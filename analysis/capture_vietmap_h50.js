'use strict';

// Runtime instrumentation for VIETMAP Live 3.2.6.
// Captures BLE GATT traffic and plaintext navigation state. When the runner
// enables VMH50_RELAY, it also sends a documented VMSL frame to the ESP32.

setImmediate(function () {
  console.log('[VMH50] java_bridge available=' + Java.available);
  if (!Java.available) {
    console.log('[VMH50] Java VM is not available in this process');
    return;
  }

  Java.perform(function () {
    function now() {
      return new Date().toISOString();
    }

    function emit(kind, fields) {
      fields.ts = now();
      fields.kind = kind;
      console.log('[VMH50] ' + JSON.stringify(fields));
    }

    function hex(bytes) {
      if (bytes === null || bytes === undefined) return null;
      var out = '';
      for (var i = 0; i < bytes.length; i++) {
        var value = bytes[i];
        if (value < 0) value += 256;
        out += ('0' + value.toString(16)).slice(-2);
      }
      return out;
    }

    function safe(callable, fallback) {
      try {
        var value = callable();
        return value === null || value === undefined ? fallback : String(value);
      } catch (_) {
        return fallback;
      }
    }

    function characteristicInfo(gatt, characteristic, bytes, writeType) {
      return {
        device_name: safe(function () { return gatt.getDevice().getName(); }, null),
        device_address: safe(function () { return gatt.getDevice().getAddress(); }, null),
        service_uuid: safe(function () { return characteristic.getService().getUuid(); }, null),
        characteristic_uuid: safe(function () { return characteristic.getUuid(); }, null),
        write_type: writeType,
        length: bytes === null || bytes === undefined ? null : bytes.length,
        value_hex: hex(bytes)
      };
    }

    function parseByte(value, fallback) {
      var parsed = parseInt(value, 10);
      if (isNaN(parsed)) return fallback;
      return Math.max(0, Math.min(255, parsed));
    }

    var relayEnabled = typeof VMH50_RELAY !== 'undefined' && VMH50_RELAY === true;
    var BluetoothGatt = Java.use('android.bluetooth.BluetoothGatt');
    var activeGatt = null;
    var activeCharacteristic = null;
    var latestSpeedLimit = null;
    var latestCurrentSpeed = 0;
    var lastRelayedLimit = null;
    var relayScheduled = false;
    var relayWriteActive = false;
    var writeLegacy = null;

    function isTargetHud(gatt, characteristic) {
      var name = safe(function () { return gatt.getDevice().getName(); }, '');
      var serviceUuid = safe(function () { return characteristic.getService().getUuid(); }, '').toLowerCase();
      var characteristicUuid = safe(function () { return characteristic.getUuid(); }, '').toLowerCase();
      return name === 'VIETMAP_HUD_H50'
        && serviceUuid === '0000ffff-0000-1000-8000-00805f9b34fb'
        && characteristicUuid === '00009abc-0000-1000-8000-00805f9b34fb';
    }

    // Frame: "VMSL", version, speedLimit, currentSpeed, XOR checksum.
    function buildRelayFrame(speedLimit, currentSpeed) {
      var values = [0x56, 0x4d, 0x53, 0x4c, 0x01, speedLimit, currentSpeed];
      var checksum = 0;
      for (var i = 0; i < values.length; i++) checksum ^= values[i];
      values.push(checksum);
      for (var j = 0; j < values.length; j++) {
        if (values[j] > 127) values[j] -= 256;
      }
      return Java.array('byte', values);
    }

    function scheduleRelay(reason) {
      if (!relayEnabled || relayScheduled || relayWriteActive || writeLegacy === null) return;
      if (activeGatt === null || activeCharacteristic === null || latestSpeedLimit === null) return;
      if (latestSpeedLimit === lastRelayedLimit) return;

      relayScheduled = true;
      setTimeout(function () {
        Java.perform(function () {
          relayScheduled = false;
          try {
            var packet = buildRelayFrame(latestSpeedLimit, latestCurrentSpeed);
            activeCharacteristic.setValue(packet);
            activeCharacteristic.setWriteType(2);

            var accepted = false;
            relayWriteActive = true;
            try {
              accepted = writeLegacy.call(activeGatt, activeCharacteristic);
            } finally {
              relayWriteActive = false;
            }

            if (accepted) lastRelayedLimit = latestSpeedLimit;
            emit('speed_limit_relay', {
              reason: reason,
              accepted: Boolean(accepted),
              speed_limit: latestSpeedLimit,
              current_speed: latestCurrentSpeed,
              value_hex: hex(packet)
            });
          } catch (error) {
            emit('speed_limit_relay_error', { reason: reason, error: String(error) });
          }
        });
      }, 700);
    }

    // Android <= 12 / legacy overload (Pixel 3 XL uses this overload).
    try {
      writeLegacy = BluetoothGatt.writeCharacteristic.overload(
        'android.bluetooth.BluetoothGattCharacteristic'
      );
      writeLegacy.implementation = function (characteristic) {
        var bytes = characteristic.getValue();
        var targetHud = isTargetHud(this, characteristic);
        var appHudWrite = targetHud && !relayWriteActive;
        if (appHudWrite) {
          var newGatt = activeGatt === null
            || safe(function () { return activeGatt.equals(this); }.bind(this), 'false') !== 'true';
          activeGatt = Java.retain(this);
          activeCharacteristic = Java.retain(characteristic);
          if (newGatt) lastRelayedLimit = null;
        }

        emit('gatt_tx', characteristicInfo(
          this,
          characteristic,
          bytes,
          safe(function () { return characteristic.getWriteType(); }, null)
        ));

        var accepted = writeLegacy.call(this, characteristic);
        if (appHudWrite) scheduleRelay('after_h50_write');
        return accepted;
      };
      emit('hook_ready', { target: 'BluetoothGatt.writeCharacteristic(legacy)' });
    } catch (error) {
      emit('hook_unavailable', { target: 'BluetoothGatt.writeCharacteristic(legacy)', error: String(error) });
    }

    // Android 13+ overload, absent on this Pixel's Android version.
    try {
      var writeApi33 = BluetoothGatt.writeCharacteristic.overload(
        'android.bluetooth.BluetoothGattCharacteristic',
        '[B',
        'int'
      );
      writeApi33.implementation = function (characteristic, bytes, writeType) {
        emit('gatt_tx', characteristicInfo(this, characteristic, bytes, writeType));
        return writeApi33.call(this, characteristic, bytes, writeType);
      };
      emit('hook_ready', { target: 'BluetoothGatt.writeCharacteristic(api33)' });
    } catch (error) {
      emit('hook_unavailable', { target: 'BluetoothGatt.writeCharacteristic(api33)', error: String(error) });
    }

    try {
      var FbpGattCallback = Java.use('i6.g$d');
      var changed = FbpGattCallback.onCharacteristicChanged.overload(
        'android.bluetooth.BluetoothGatt',
        'android.bluetooth.BluetoothGattCharacteristic',
        '[B'
      );
      changed.implementation = function (gatt, characteristic, bytes) {
        emit('gatt_rx', characteristicInfo(gatt, characteristic, bytes, null));
        return changed.call(this, gatt, characteristic, bytes);
      };
      emit('hook_ready', { target: 'i6.g$d.onCharacteristicChanged' });

      var written = FbpGattCallback.onCharacteristicWrite.overload(
        'android.bluetooth.BluetoothGatt',
        'android.bluetooth.BluetoothGattCharacteristic',
        'int'
      );
      written.implementation = function (gatt, characteristic, status) {
        var info = characteristicInfo(gatt, characteristic, characteristic.getValue(), null);
        info.status = status;
        emit('gatt_write_complete', info);
        return written.call(this, gatt, characteristic, status);
      };
      emit('hook_ready', { target: 'i6.g$d.onCharacteristicWrite' });

      var connectionChanged = FbpGattCallback.onConnectionStateChange.overload(
        'android.bluetooth.BluetoothGatt',
        'int',
        'int'
      );
      connectionChanged.implementation = function (gatt, status, newState) {
        emit('gatt_connection', {
          device_name: safe(function () { return gatt.getDevice().getName(); }, null),
          device_address: safe(function () { return gatt.getDevice().getAddress(); }, null),
          status: status,
          new_state: newState
        });
        return connectionChanged.call(this, gatt, status, newState);
      };
      emit('hook_ready', { target: 'i6.g$d.onConnectionStateChange' });

      try {
        var mtuChanged = FbpGattCallback.onMtuChanged.overload(
          'android.bluetooth.BluetoothGatt',
          'int',
          'int'
        );
        mtuChanged.implementation = function (gatt, mtu, status) {
          emit('gatt_mtu', {
            device_address: safe(function () { return gatt.getDevice().getAddress(); }, null),
            mtu: mtu,
            status: status
          });
          return mtuChanged.call(this, gatt, mtu, status);
        };
        emit('hook_ready', { target: 'i6.g$d.onMtuChanged' });
      } catch (error) {
        emit('hook_unavailable', { target: 'i6.g$d.onMtuChanged', error: String(error) });
      }
    } catch (error) {
      emit('hook_unavailable', { target: 'i6.g$d callbacks', error: String(error) });
    }

    // Plaintext path used by the native Android Auto renderer.
    try {
      var OverlayModel = Java.use('Z9.f');
      var updateOverlay = OverlayModel.q.overload('org.json.JSONObject');
      updateOverlay.implementation = function (json) {
        var speedLimitText = safe(function () { return json.opt('speedLimit'); }, null);
        var currentSpeedText = safe(function () { return json.opt('speed'); }, null);
        var parsedLimit = parseByte(speedLimitText, null);
        var parsedCurrent = parseByte(currentSpeedText, 0);
        var limitChanged = parsedLimit !== null && parsedLimit !== latestSpeedLimit;
        latestSpeedLimit = parsedLimit;
        latestCurrentSpeed = parsedCurrent;

        emit('plaintext_navigation_state', {
          speed_limit: speedLimitText,
          current_speed: currentSpeedText,
          hud_connected: safe(function () { return json.opt('hudConnected'); }, null),
          lane_speed_limits: safe(function () { return json.opt('laneSpeedLimits'); }, null)
        });

        if (limitChanged) scheduleRelay('speed_limit_changed', false);
        return updateOverlay.call(this, json);
      };
      emit('hook_ready', { target: 'Z9.f.q(JSONObject)' });
    } catch (error) {
      emit('hook_unavailable', { target: 'Z9.f.q(JSONObject)', error: String(error) });
    }

    emit('capture_ready', {
      package: 'vn.vietmap.live',
      version: '3.2.6',
      relay_enabled: relayEnabled,
      relay_frame: '564d534c01LLCCXX'
    });
  });
});
