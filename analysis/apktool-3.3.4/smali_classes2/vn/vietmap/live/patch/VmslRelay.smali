.class public final Lvn/vietmap/live/patch/VmslRelay;
.super Ljava/lang/Object;

.field private static final SERVICE_UUID:Ljava/util/UUID;
.field private static final WRITE_UUID:Ljava/util/UUID;

.field private static hasState:Z
.field private static speedLimit:I
.field private static currentSpeed:I
.field private static dirty:Z

.field private static hasExt:Z
.field private static extFlags:I
.field private static minSpeedLimit:I
.field private static navState:I
.field private static alertDistance:I
.field private static alertSpeedLimit:I

.field private static gatt:Landroid/bluetooth/BluetoothGatt;
.field private static characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;

.field private static relayWriteActive:Z
.field private static relayGatt:Landroid/bluetooth/BluetoothGatt;
.field private static relayCharacteristic:Landroid/bluetooth/BluetoothGattCharacteristic;

.method static constructor <clinit>()V
    .locals 1

    const-string v0, "0000ffff-0000-1000-8000-00805f9b34fb"
    invoke-static {v0}, Ljava/util/UUID;->fromString(Ljava/lang/String;)Ljava/util/UUID;
    move-result-object v0
    sput-object v0, Lvn/vietmap/live/patch/VmslRelay;->SERVICE_UUID:Ljava/util/UUID;

    const-string v0, "00009abc-0000-1000-8000-00805f9b34fb"
    invoke-static {v0}, Ljava/util/UUID;->fromString(Ljava/lang/String;)Ljava/util/UUID;
    move-result-object v0
    sput-object v0, Lvn/vietmap/live/patch/VmslRelay;->WRITE_UUID:Ljava/util/UUID;

    return-void
.end method

.method public static final updateFromJson(Lorg/json/JSONObject;)V
    .locals 1

    :try_start_0
    invoke-static {p0}, Lvn/vietmap/live/patch/VmslRelay;->handleJson(Lorg/json/JSONObject;)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    return-void

    :catchall_0
    move-exception v0
    return-void
.end method

.method private static final declared-synchronized handleJson(Lorg/json/JSONObject;)V
    .locals 12

    if-nez p0, :json_present
    return-void

    :json_present
    const-string v0, "speedLimit"
    invoke-virtual {p0, v0}, Lorg/json/JSONObject;->isNull(Ljava/lang/String;)Z
    move-result v1
    if-eqz v1, :json_has_limit

    const/4 v0, 0x0
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    return-void

    :json_has_limit
    const/4 v1, 0x0
    invoke-virtual {p0, v0, v1}, Lorg/json/JSONObject;->optInt(Ljava/lang/String;I)I
    move-result v0
    invoke-static {v0}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v0

    const-string v2, "speed"
    invoke-virtual {p0, v2, v1}, Lorg/json/JSONObject;->optInt(Ljava/lang/String;I)I
    move-result v2
    invoke-static {v2}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v2

    const-string v3, "minSpeedLimit"
    invoke-virtual {p0, v3, v1}, Lorg/json/JSONObject;->optInt(Ljava/lang/String;I)I
    move-result v3
    invoke-static {v3}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v3

    const-string v4, "navigationState"
    invoke-virtual {p0, v4, v1}, Lorg/json/JSONObject;->optInt(Ljava/lang/String;I)I
    move-result v4
    invoke-static {v4}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v4

    const/4 v5, 0x0

    const-string v6, "overSpeed"
    invoke-virtual {p0, v6, v5}, Lorg/json/JSONObject;->optBoolean(Ljava/lang/String;Z)Z
    move-result v6
    if-eqz v6, :flag_over_done
    const/4 v6, 0x1
    goto :flag_over_set

    :flag_over_done
    const/4 v6, 0x0

    :flag_over_set
    const-string v7, "underMinSpeedLimit"
    invoke-virtual {p0, v7, v5}, Lorg/json/JSONObject;->optBoolean(Ljava/lang/String;Z)Z
    move-result v7
    if-eqz v7, :flag_under_done
    const/4 v7, 0x2
    goto :flag_under_set

    :flag_under_done
    const/4 v7, 0x0

    :flag_under_set
    or-int/2addr v6, v7

    const-string v7, "hudConnected"
    invoke-virtual {p0, v7, v5}, Lorg/json/JSONObject;->optBoolean(Ljava/lang/String;Z)Z
    move-result v7
    if-eqz v7, :flag_hud_done
    const/4 v7, 0x4
    goto :flag_hud_set

    :flag_hud_done
    const/4 v7, 0x0

    :flag_hud_set
    or-int/2addr v6, v7

    const/4 v8, 0x0
    const/4 v9, 0x0

    const-string v7, "upcomingAlerts"
    invoke-virtual {p0, v7}, Lorg/json/JSONObject;->optJSONArray(Ljava/lang/String;)Lorg/json/JSONArray;
    move-result-object v7
    if-eqz v7, :alert_done

    invoke-virtual {v7}, Lorg/json/JSONArray;->length()I
    move-result v10
    if-lez v10, :alert_done

    invoke-virtual {v7, v1}, Lorg/json/JSONArray;->optJSONObject(I)Lorg/json/JSONObject;
    move-result-object v7
    if-eqz v7, :alert_done

    const-string v10, "distance"
    invoke-virtual {v7, v10, v1}, Lorg/json/JSONObject;->optInt(Ljava/lang/String;I)I
    move-result v8
    if-gez v8, :alert_distance_ok
    const/4 v8, 0x0

    :alert_distance_ok
    const v10, 0xffff
    if-le v8, v10, :alert_distance_ready
    move v8, v10

    :alert_distance_ready
    const-string v10, "speedLimit"
    invoke-virtual {v7, v10, v1}, Lorg/json/JSONObject;->optInt(Ljava/lang/String;I)I
    move-result v9
    invoke-static {v9}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v9

    const/16 v10, 0x8
    or-int/2addr v6, v10

    :alert_done
    const/4 v10, 0x0

    sget-boolean v11, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    if-eqz v11, :json_changed

    sget-boolean v11, Lvn/vietmap/live/patch/VmslRelay;->hasExt:Z
    if-eqz v11, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    if-ne v11, v0, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    if-ne v11, v2, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->minSpeedLimit:I
    if-ne v11, v3, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->navState:I
    if-ne v11, v4, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->extFlags:I
    if-ne v11, v6, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->alertDistance:I
    if-ne v11, v8, :json_changed

    sget v11, Lvn/vietmap/live/patch/VmslRelay;->alertSpeedLimit:I
    if-ne v11, v9, :json_changed

    goto :json_store

    :json_changed
    const/4 v10, 0x1

    :json_store
    sput v0, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    sput v2, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    sput v3, Lvn/vietmap/live/patch/VmslRelay;->minSpeedLimit:I
    sput v4, Lvn/vietmap/live/patch/VmslRelay;->navState:I
    sput v6, Lvn/vietmap/live/patch/VmslRelay;->extFlags:I
    sput v8, Lvn/vietmap/live/patch/VmslRelay;->alertDistance:I
    sput v9, Lvn/vietmap/live/patch/VmslRelay;->alertSpeedLimit:I

    const/4 v11, 0x1
    sput-boolean v11, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    sput-boolean v11, Lvn/vietmap/live/patch/VmslRelay;->hasExt:Z

    if-eqz v10, :json_done
    sput-boolean v11, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z

    :json_done
    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eqz v0, :json_exit

    sget-object v1, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    if-eqz v1, :json_exit

    invoke-static {v0, v1}, Lvn/vietmap/live/patch/VmslRelay;->trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z

    :json_exit
    return-void
.end method

.method public static final updateState(Ljava/lang/Integer;Ljava/lang/Integer;)V
    .locals 1

    :try_start_0
    invoke-static {p0, p1}, Lvn/vietmap/live/patch/VmslRelay;->handleUpdate(Ljava/lang/Integer;Ljava/lang/Integer;)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    return-void

    :catchall_0
    move-exception v0
    return-void
.end method

.method private static final declared-synchronized handleUpdate(Ljava/lang/Integer;Ljava/lang/Integer;)V
    .locals 4

    if-nez p0, :have_limit

    const/4 v0, 0x0
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    return-void

    :have_limit
    invoke-virtual {p0}, Ljava/lang/Integer;->intValue()I
    move-result v0
    invoke-static {v0}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v0

    if-nez p1, :have_current
    const/4 v1, 0x0
    goto :current_ready

    :have_current
    invoke-virtual {p1}, Ljava/lang/Integer;->intValue()I
    move-result v1
    invoke-static {v1}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v1

    :current_ready
    sget-boolean v2, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    if-eqz v2, :mark_dirty

    sget v2, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    if-ne v2, v0, :mark_dirty

    sget v2, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    if-eq v2, v1, :store_state

    :mark_dirty
    const/4 v2, 0x1
    sput-boolean v2, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z

    :store_state
    sput v0, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    sput v1, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    const/4 v2, 0x1
    sput-boolean v2, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z

    sget-object v2, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eqz v2, :done

    sget-object v3, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    if-eqz v3, :done

    invoke-static {v2, v3}, Lvn/vietmap/live/patch/VmslRelay;->trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z

    :done
    return-void
.end method

.method public static final updatePhoneSpeedLimit(Ljava/lang/Integer;)V
    .locals 1

    :try_start_0
    invoke-static {p0}, Lvn/vietmap/live/patch/VmslRelay;->handlePhoneSpeedLimit(Ljava/lang/Integer;)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    return-void

    :catchall_0
    move-exception v0
    return-void
.end method

.method private static final declared-synchronized handlePhoneSpeedLimit(Ljava/lang/Integer;)V
    .locals 3

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasExt:Z
    if-eqz v0, :phone_limit_allowed
    return-void

    :phone_limit_allowed
    if-nez p0, :have_phone_limit

    const/4 v0, 0x0
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    return-void

    :have_phone_limit
    invoke-virtual {p0}, Ljava/lang/Integer;->intValue()I
    move-result v0
    invoke-static {v0}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v0

    sget-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    if-eqz v1, :mark_phone_dirty

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    if-eq v1, v0, :store_phone_limit

    :mark_phone_dirty
    const/4 v1, 0x1
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z

    :store_phone_limit
    sput v0, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    const/4 v1, 0x1
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z

    sget-object v1, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eqz v1, :phone_limit_done

    sget-object v2, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    if-eqz v2, :phone_limit_done

    invoke-static {v1, v2}, Lvn/vietmap/live/patch/VmslRelay;->trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z

    :phone_limit_done
    return-void
.end method

.method public static final updatePhoneCurrentSpeed(Ljava/lang/Integer;)V
    .locals 1

    :try_start_0
    invoke-static {p0}, Lvn/vietmap/live/patch/VmslRelay;->handlePhoneCurrentSpeed(Ljava/lang/Integer;)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    return-void

    :catchall_0
    move-exception v0
    return-void
.end method

.method private static final declared-synchronized handlePhoneCurrentSpeed(Ljava/lang/Integer;)V
    .locals 3

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasExt:Z
    if-eqz v0, :phone_speed_allowed
    return-void

    :phone_speed_allowed
    if-nez p0, :have_phone_speed

    const/4 v0, 0x0
    goto :phone_speed_ready

    :have_phone_speed
    invoke-virtual {p0}, Ljava/lang/Integer;->intValue()I
    move-result v0
    invoke-static {v0}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result v0

    :phone_speed_ready
    sget v1, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    if-eq v1, v0, :phone_speed_done

    sput v0, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I

    sget-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    if-eqz v1, :phone_speed_done

    const/4 v1, 0x1
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z

    sget-object v1, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eqz v1, :phone_speed_done

    sget-object v2, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    if-eqz v2, :phone_speed_done

    invoke-static {v1, v2}, Lvn/vietmap/live/patch/VmslRelay;->trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z

    :phone_speed_done
    return-void
.end method

.method public static final onCharacteristicWrite(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;I)Z
    .locals 1

    :try_start_0
    invoke-static {p0, p1, p2}, Lvn/vietmap/live/patch/VmslRelay;->handleWrite(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;I)Z
    move-result v0
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    return v0

    :catchall_0
    move-exception v0
    const/4 v0, 0x0
    return v0
.end method

.method private static final declared-synchronized handleWrite(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;I)Z
    .locals 3

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->relayWriteActive:Z
    if-eqz v0, :not_relay

    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->relayGatt:Landroid/bluetooth/BluetoothGatt;
    if-ne p0, v0, :not_relay

    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->relayCharacteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    if-ne p1, v0, :not_relay

    const/4 v0, 0x0
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->relayWriteActive:Z
    const/4 v1, 0x0
    sput-object v1, Lvn/vietmap/live/patch/VmslRelay;->relayGatt:Landroid/bluetooth/BluetoothGatt;
    sput-object v1, Lvn/vietmap/live/patch/VmslRelay;->relayCharacteristic:Landroid/bluetooth/BluetoothGattCharacteristic;

    if-eqz p2, :relay_success

    const/4 v0, 0x1
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    goto :relay_done

    :relay_success
    invoke-static {p0, p1}, Lvn/vietmap/live/patch/VmslRelay;->trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z

    :relay_done
    const/4 v0, 0x1
    return v0

    :not_relay
    if-nez p2, :not_consumed

    invoke-static {p1}, Lvn/vietmap/live/patch/VmslRelay;->isH50WriteCharacteristic(Landroid/bluetooth/BluetoothGattCharacteristic;)Z
    move-result v0
    if-eqz v0, :not_consumed

    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eq p0, v0, :same_gatt

    sput-object p0, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    sput-object p1, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    const/4 v0, 0x1
    sput-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    goto :try_send

    :same_gatt
    sput-object p1, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;

    :try_send
    invoke-static {p0, p1}, Lvn/vietmap/live/patch/VmslRelay;->trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z

    :not_consumed
    const/4 v0, 0x0
    return v0
.end method

.method private static final isH50WriteCharacteristic(Landroid/bluetooth/BluetoothGattCharacteristic;)Z
    .locals 3

    if-eqz p0, :not_h50

    invoke-virtual {p0}, Landroid/bluetooth/BluetoothGattCharacteristic;->getUuid()Ljava/util/UUID;
    move-result-object v0
    sget-object v1, Lvn/vietmap/live/patch/VmslRelay;->WRITE_UUID:Ljava/util/UUID;
    invoke-virtual {v1, v0}, Ljava/util/UUID;->equals(Ljava/lang/Object;)Z
    move-result v0
    if-eqz v0, :not_h50

    invoke-virtual {p0}, Landroid/bluetooth/BluetoothGattCharacteristic;->getService()Landroid/bluetooth/BluetoothGattService;
    move-result-object v0
    if-eqz v0, :not_h50

    invoke-virtual {v0}, Landroid/bluetooth/BluetoothGattService;->getUuid()Ljava/util/UUID;
    move-result-object v0
    sget-object v1, Lvn/vietmap/live/patch/VmslRelay;->SERVICE_UUID:Ljava/util/UUID;
    invoke-virtual {v1, v0}, Ljava/util/UUID;->equals(Ljava/lang/Object;)Z
    move-result v0
    return v0

    :not_h50
    const/4 v0, 0x0
    return v0
.end method

.method private static final declared-synchronized trySendLocked(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)Z
    .locals 4

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    if-eqz v0, :not_sent

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    if-eqz v0, :not_sent

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->relayWriteActive:Z
    if-nez v0, :not_sent

    if-eqz p0, :not_sent
    if-eqz p1, :not_sent

    invoke-static {p1}, Lvn/vietmap/live/patch/VmslRelay;->isH50WriteCharacteristic(Landroid/bluetooth/BluetoothGattCharacteristic;)Z
    move-result v0
    if-eqz v0, :not_sent

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasExt:Z
    if-eqz v0, :build_basic

    invoke-static {}, Lvn/vietmap/live/patch/VmslRelay;->buildFrameExtended()[B
    move-result-object v0
    goto :frame_ready

    :build_basic
    sget v0, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    sget v1, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    invoke-static {v0, v1}, Lvn/vietmap/live/patch/VmslRelay;->buildFrame(II)[B
    move-result-object v0

    :frame_ready
    const/4 v1, 0x2
    invoke-virtual {p1, v1}, Landroid/bluetooth/BluetoothGattCharacteristic;->setWriteType(I)V

    invoke-virtual {p1, v0}, Landroid/bluetooth/BluetoothGattCharacteristic;->setValue([B)Z
    move-result v1
    if-eqz v1, :not_sent

    const/4 v1, 0x1
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->relayWriteActive:Z
    sput-object p0, Lvn/vietmap/live/patch/VmslRelay;->relayGatt:Landroid/bluetooth/BluetoothGatt;
    sput-object p1, Lvn/vietmap/live/patch/VmslRelay;->relayCharacteristic:Landroid/bluetooth/BluetoothGattCharacteristic;

    :try_start_0
    invoke-virtual {p0, p1}, Landroid/bluetooth/BluetoothGatt;->writeCharacteristic(Landroid/bluetooth/BluetoothGattCharacteristic;)Z
    move-result v1
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :write_failed

    if-eqz v1, :clear_failed_write

    const/4 v1, 0x0
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z
    const/4 v1, 0x1
    return v1

    :write_failed
    move-exception v2

    :clear_failed_write
    const/4 v1, 0x0
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->relayWriteActive:Z
    const/4 v2, 0x0
    sput-object v2, Lvn/vietmap/live/patch/VmslRelay;->relayGatt:Landroid/bluetooth/BluetoothGatt;
    sput-object v2, Lvn/vietmap/live/patch/VmslRelay;->relayCharacteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    const/4 v2, 0x1
    sput-boolean v2, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z

    :not_sent
    const/4 v0, 0x0
    return v0
.end method

.method private static final buildFrame(II)[B
    .locals 4

    invoke-static {p0}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result p0
    invoke-static {p1}, Lvn/vietmap/live/patch/VmslRelay;->clampByte(I)I
    move-result p1

    const/16 v0, 0x8
    new-array v0, v0, [B

    const/16 v1, 0x56
    const/4 v2, 0x0
    aput-byte v1, v0, v2
    const/16 v1, 0x4d
    const/4 v2, 0x1
    aput-byte v1, v0, v2
    const/16 v1, 0x53
    const/4 v2, 0x2
    aput-byte v1, v0, v2
    const/16 v1, 0x4c
    const/4 v2, 0x3
    aput-byte v1, v0, v2
    const/4 v1, 0x1
    const/4 v2, 0x4
    aput-byte v1, v0, v2
    const/4 v2, 0x5
    aput-byte p0, v0, v2
    const/4 v2, 0x6
    aput-byte p1, v0, v2

    const/4 v1, 0x0
    const/4 v2, 0x0

    :checksum_loop
    const/4 v3, 0x7
    if-ge v2, v3, :checksum_done
    aget-byte v3, v0, v2
    and-int/lit16 v3, v3, 0xff
    xor-int/2addr v1, v3
    add-int/lit8 v2, v2, 0x1
    goto :checksum_loop

    :checksum_done
    const/4 v2, 0x7
    aput-byte v1, v0, v2
    return-object v0
.end method

.method private static final buildFrameExtended()[B
    .locals 6

    const/16 v0, 0xe
    new-array v0, v0, [B

    const/16 v1, 0x56
    const/4 v2, 0x0
    aput-byte v1, v0, v2
    const/16 v1, 0x4d
    const/4 v2, 0x1
    aput-byte v1, v0, v2
    const/16 v1, 0x53
    const/4 v2, 0x2
    aput-byte v1, v0, v2
    const/16 v1, 0x58
    const/4 v2, 0x3
    aput-byte v1, v0, v2
    const/4 v1, 0x1
    const/4 v2, 0x4
    aput-byte v1, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->speedLimit:I
    int-to-byte v1, v1
    const/4 v2, 0x5
    aput-byte v1, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->currentSpeed:I
    int-to-byte v1, v1
    const/4 v2, 0x6
    aput-byte v1, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->extFlags:I
    int-to-byte v1, v1
    const/4 v2, 0x7
    aput-byte v1, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->minSpeedLimit:I
    int-to-byte v1, v1
    const/16 v2, 0x8
    aput-byte v1, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->navState:I
    int-to-byte v1, v1
    const/16 v2, 0x9
    aput-byte v1, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->alertDistance:I
    shr-int/lit8 v3, v1, 0x8
    and-int/lit16 v3, v3, 0xff
    int-to-byte v3, v3
    const/16 v2, 0xa
    aput-byte v3, v0, v2

    and-int/lit16 v3, v1, 0xff
    int-to-byte v3, v3
    const/16 v2, 0xb
    aput-byte v3, v0, v2

    sget v1, Lvn/vietmap/live/patch/VmslRelay;->alertSpeedLimit:I
    int-to-byte v1, v1
    const/16 v2, 0xc
    aput-byte v1, v0, v2

    const/4 v1, 0x0
    const/4 v2, 0x0

    :checksum_ext_loop
    const/16 v3, 0xd
    if-ge v2, v3, :checksum_ext_done
    aget-byte v3, v0, v2
    and-int/lit16 v3, v3, 0xff
    xor-int/2addr v1, v3
    add-int/lit8 v2, v2, 0x1
    goto :checksum_ext_loop

    :checksum_ext_done
    int-to-byte v1, v1
    const/16 v2, 0xd
    aput-byte v1, v0, v2
    return-object v0
.end method

.method private static final clampByte(I)I
    .locals 1

    if-gez p0, :not_negative
    const/4 v0, 0x0
    return v0

    :not_negative
    const/16 v0, 0xff
    if-le p0, v0, :in_range
    return v0

    :in_range
    return p0
.end method

.method public static final registerGatt(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)V
    .locals 1

    :try_start_0
    invoke-static {p0, p1}, Lvn/vietmap/live/patch/VmslRelay;->handleRegisterGatt(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    return-void

    :catchall_0
    move-exception v0
    return-void
.end method

.method private static final declared-synchronized handleRegisterGatt(Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)V
    .locals 2

    if-eqz p0, :done
    if-eqz p1, :done

    invoke-static {p1}, Lvn/vietmap/live/patch/VmslRelay;->isH50WriteCharacteristic(Landroid/bluetooth/BluetoothGattCharacteristic;)Z
    move-result v0
    if-eqz v0, :done

    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eq p0, v0, :same_gatt

    const/4 v1, 0x1
    sput-boolean v1, Lvn/vietmap/live/patch/VmslRelay;->dirty:Z

    :same_gatt
    sput-object p0, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    sput-object p1, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;

    :done
    return-void
.end method

.method public static final declared-synchronized shouldKeepConnection()Z
    .locals 1

    sget-boolean v0, Lvn/vietmap/live/patch/VmslRelay;->hasState:Z
    if-eqz v0, :do_not_keep

    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->gatt:Landroid/bluetooth/BluetoothGatt;
    if-eqz v0, :do_not_keep

    sget-object v0, Lvn/vietmap/live/patch/VmslRelay;->characteristic:Landroid/bluetooth/BluetoothGattCharacteristic;
    if-eqz v0, :do_not_keep

    const/4 v0, 0x1
    return v0

    :do_not_keep
    const/4 v0, 0x0
    return v0
.end method
