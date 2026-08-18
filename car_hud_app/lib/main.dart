import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

// Phai khop chinh xac voi main/ble/ble_car_service.h ben firmware (dang gia
// lap GATT cua thiet bi VIETMAP HUD H50 de tuong thich voi app h50Handler).
const String kDeviceName = 'VIETMAP_HUD_H1N';
final Guid kServiceUuid = Guid('0000FFFF-0000-1000-8000-00805F9B34FB');
// Characteristic Write - dien thoai ghi du lieu xuong board (dinh dang tu
// quy uoc rieng cua du an nay: speed uint16 LE + limit uint16 LE).
final Guid kCharUuid = Guid('00009ABC-0000-1000-8000-00805F9B34FB');

void main() {
  // Bat log chi tiet nhat cua flutter_blue_plus (goi thang xuong Android/iOS
  // BLE stack goc) - dung de debug ly do ngat ket noi, xem `flutter logs` hoac
  // Logcat (loc theo tag "flutter"). Xem thong tin nhu GATT status code that
  // tu he dieu hanh, thu tu goi callback discoverServices, v.v.
  FlutterBluePlus.setLogLevel(LogLevel.verbose);
  runApp(const CarHudApp());
}

class CarHudApp extends StatelessWidget {
  const CarHudApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Car HUD',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue, brightness: Brightness.dark),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

enum ConnStatus { disconnected, scanning, connecting, connected }

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  BluetoothDevice? _device;
  BluetoothCharacteristic? _char;
  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<BluetoothConnectionState>? _connSub;

  ConnStatus _status = ConnStatus.disconnected;
  String _statusText = 'Chưa kết nối';

  double _speed = 0;
  double _limit = 50;

  @override
  void dispose() {
    _scanSub?.cancel();
    _connSub?.cancel();
    super.dispose();
  }

  Future<void> _requestPermissions() async {
    await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();
  }

  Future<void> _scanAndConnect() async {
    await _requestPermissions();
    setState(() {
      _status = ConnStatus.scanning;
      _statusText = 'Đang quét "$kDeviceName"...';
    });

    bool found = false;

    _scanSub = FlutterBluePlus.scanResults.listen((results) async {
      if (found) return;
      for (final r in results) {
        final name = r.device.platformName.isNotEmpty ? r.device.platformName : r.advertisementData.advName;
        if (name == kDeviceName) {
          found = true;
          await FlutterBluePlus.stopScan();
          await _scanSub?.cancel();
          await _connectTo(r.device);
          break;
        }
      }
    });

    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));

    await Future.delayed(const Duration(seconds: 10));
    if (!found && mounted) {
      setState(() {
        _status = ConnStatus.disconnected;
        _statusText = 'Không tìm thấy "$kDeviceName". Kiểm tra board đã bật Car Mode chưa.';
      });
    }
  }

  Future<void> _connectTo(BluetoothDevice device) async {
    _device = device;
    setState(() {
      _status = ConnStatus.connecting;
      _statusText = 'Đang kết nối...';
    });

    _connSub?.cancel();
    _connSub = device.connectionState.listen((state) {
      debugPrint('[CarHUD] connectionState -> $state luc ${DateTime.now()}');
      if (state == BluetoothConnectionState.disconnected && mounted) {
        setState(() {
          _status = ConnStatus.disconnected;
          _statusText = 'Mất kết nối';
          _char = null;
        });
      }
    });

    try {
      debugPrint('[CarHUD] Bat dau connect() luc ${DateTime.now()}');
      // flutter_blue_plus >= 2.x yeu cau khai bao License khi ket noi.
      // Du an ca nhan/phi loi nhuan -> dung License.nonprofit (mien phi).
      // Neu dung cho muc dich thuong mai sau nay, phai doi sang
      // License.commercial (co phi) theo dieu khoan cua thu vien.
      await device.connect(license: License.nonprofit, timeout: const Duration(seconds: 12));
      debugPrint('[CarHUD] connect() xong luc ${DateTime.now()}, bat dau discoverServices()');

      final services = await device.discoverServices();
      debugPrint('[CarHUD] discoverServices() xong luc ${DateTime.now()}, tim thay ${services.length} service:');
      for (final s in services) {
        debugPrint('[CarHUD]   service ${s.uuid} (${s.characteristics.length} characteristic)');
        for (final c in s.characteristics) {
          debugPrint('[CarHUD]     characteristic ${c.uuid} properties=${c.properties}');
        }
      }

      BluetoothCharacteristic? found;
      for (final s in services) {
        if (s.uuid == kServiceUuid) {
          for (final c in s.characteristics) {
            if (c.uuid == kCharUuid) {
              found = c;
            }
          }
        }
      }
      _char = found;
      debugPrint('[CarHUD] Service/characteristic can tim: ${found != null ? "DA TIM THAY" : "KHONG TIM THAY"}');

      if (!mounted) return;
      if (_char == null) {
        setState(() {
          _status = ConnStatus.disconnected;
          _statusText = 'Đã kết nối nhưng không thấy đúng service/characteristic';
        });
        return;
      }

      setState(() {
        _status = ConnStatus.connected;
        _statusText = 'Đã kết nối: ${device.platformName}';
      });
      await _sendData();
      debugPrint('[CarHUD] Da gui du lieu dau tien luc ${DateTime.now()}');
    } catch (e, st) {
      debugPrint('[CarHUD] LOI trong qua trinh ket noi/discover luc ${DateTime.now()}: $e\n$st');
      if (!mounted) return;
      setState(() {
        _status = ConnStatus.disconnected;
        _statusText = 'Lỗi kết nối: $e';
      });
    }
  }

  Future<void> _disconnect() async {
    await _device?.disconnect();
    setState(() {
      _status = ConnStatus.disconnected;
      _statusText = 'Chưa kết nối';
      _char = null;
    });
  }

  Future<void> _sendData() async {
    final c = _char;
    if (c == null) return;
    final data = ByteData(4);
    data.setUint16(0, _speed.round(), Endian.little);
    data.setUint16(2, _limit.round(), Endian.little);
    try {
      await c.write(data.buffer.asUint8List(), withoutResponse: true);
    } catch (_) {
      // Bo qua loi ghi thoang qua (vd BLE tam giat), khong lam gian doan UI.
    }
  }

  bool get _busy => _status == ConnStatus.scanning || _status == ConnStatus.connecting;

  @override
  Widget build(BuildContext context) {
    final connected = _status == ConnStatus.connected;
    return Scaffold(
      appBar: AppBar(title: const Text('Car HUD')),
      body: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Icon(
                  connected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
                  color: connected ? Colors.lightBlueAccent : Colors.grey,
                ),
                const SizedBox(width: 8),
                Expanded(child: Text(_statusText)),
              ],
            ),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: _busy ? null : (connected ? _disconnect : _scanAndConnect),
              child: Text(_busy
                  ? 'Đang xử lý...'
                  : (connected ? 'Ngắt kết nối' : 'Quét & Kết nối tới board')),
            ),
            const SizedBox(height: 40),
            Text('Tốc độ hiện tại', style: Theme.of(context).textTheme.titleMedium),
            Text('${_speed.round()} km/h', style: Theme.of(context).textTheme.displaySmall),
            Slider(
              value: _speed,
              min: 0,
              max: 200,
              divisions: 200,
              label: '${_speed.round()}',
              onChanged: (v) {
                setState(() => _speed = v);
                _sendData();
              },
            ),
            const SizedBox(height: 24),
            Text('Giới hạn tốc độ', style: Theme.of(context).textTheme.titleMedium),
            Text('${_limit.round()} km/h',
                style: Theme.of(context).textTheme.displaySmall?.copyWith(color: Colors.redAccent)),
            Slider(
              value: _limit,
              min: 0,
              max: 150,
              divisions: 150,
              activeColor: Colors.redAccent,
              label: '${_limit.round()}',
              onChanged: (v) {
                setState(() => _limit = v);
                _sendData();
              },
            ),
          ],
        ),
      ),
    );
  }
}
