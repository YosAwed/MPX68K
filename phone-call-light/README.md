# Phone Call Light System

Androidスマホに着信があったとき、Raspberry PiでパトランプをONにするシステムです。

## 必要なもの

### ハードウェア
- Raspberry Pi Zero 2 W（または他のRaspberry Pi）
- microSDカード 16GB以上
- 5V電源アダプタ
- 5V 1chリレーモジュール
- AC100V パトランプ
- ジャンパーワイヤー（メス-メス）3本

### ソフトウェア
- Raspberry Pi OS Lite（64bit推奨）
- Python 3.11+
- Tasker（Androidアプリ、有料）

---

## 配線図

```
Raspberry Pi Zero 2 W      リレーモジュール
─────────────────────      ────────────────
GPIO 17 (Pin 11)  ──────→  IN
3.3V / 5V (Pin 2) ──────→  VCC
GND       (Pin 6) ──────→  GND

リレーモジュール       パトランプ (AC100V)
────────────────       ──────────────────
COM               ──→  電源線 (片側)
NO (Normally Open)     電源線 (もう片側) → コンセントへ
```

> ⚠️ AC100V配線は感電の危険があります。コンセントを抜いた状態で作業してください。

---

## Raspberry Pi セットアップ

### 1. OSインストール
[Raspberry Pi Imager](https://www.raspberrypi.com/software/) でRaspberry Pi OS Liteを書き込む。  
書き込み時に以下を設定:
- Wi-Fi SSID / パスワード
- SSH有効化
- ユーザー名: `pi`

### 2. プロジェクトのインストール
```bash
# Raspberry Piにファイルをコピー
scp -r phone-call-light/ pi@raspberrypi.local:~/

# SSHで接続してインストール
ssh pi@raspberrypi.local
chmod +x ~/phone-call-light/setup/install.sh
~/phone-call-light/setup/install.sh
```

### 3. IPアドレスの確認
```bash
hostname -I
# 例: 192.168.1.100
```

---

## Tasker設定（Android）

着信を検知してRaspberry PiにHTTPリクエストを送ります。

### プロファイル1: 着信時にランプON

**トリガー**
- 種類: `状態 → 電話 → 電話の状態`
- 状態: `着信中`

**タスク: Call Start**
1. `ネット → HTTP リクエスト`
   - メソッド: `POST`
   - URL: `http://192.168.1.100:5000/call/start`（RasPiのIPに変更）
   - ヘッダー: `X-Auth-Token: change-me-please`（設定したトークンに変更）
   - Content-Type: `application/json`
   - Body: `{"caller": "%CNUM"}`

### プロファイル2: 通話終了時にランプOFF

**トリガー**
- 種類: `状態 → 電話 → 電話の状態`
- 状態: `アイドル`

**タスク: Call End**
1. `ネット → HTTP リクエスト`
   - メソッド: `POST`
   - URL: `http://192.168.1.100:5000/call/end`
   - ヘッダー: `X-Auth-Token: change-me-please`

---

## 動作確認

Raspberry Piにて:
```bash
# サーバーの状態確認
sudo systemctl status phone-call-light

# ログをリアルタイムで確認
journalctl -u phone-call-light -f

# 手動でランプONテスト（同じLAN内のPCから）
curl -X POST http://192.168.1.100:5000/call/start \
  -H "X-Auth-Token: change-me-please"

# ランプOFF
curl -X POST http://192.168.1.100:5000/call/end \
  -H "X-Auth-Token: change-me-please"
```

---

## 設定変更

`config.py` で以下を変更できます:

| 設定 | デフォルト | 説明 |
|------|-----------|------|
| `RELAY_PIN` | `17` | GPIOピン番号 (BCM) |
| `PORT` | `5000` | サーバーのポート番号 |
| `LAMP_OFF_DELAY` | `3` | 通話終了後にランプが消えるまでの秒数 |

認証トークンは環境変数 `LAMP_AUTH_TOKEN` または `config.py` で設定します。
