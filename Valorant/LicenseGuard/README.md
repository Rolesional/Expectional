# LicenseGuard

Drop-in lisans + anti-tamper + HWID modülü. Saf Win32 (WinHTTP, BCrypt, WMI),
hiçbir dış C++ bağımlılığı yok.

## Yapı

```
LicenseGuard/
  include/
    LicenseGuard.hpp       <- public API (sadece bunu include et)
    lic_xor.hpp            <- compile-time string obfuscation
    lic_json.hpp           <- mini JSON
    lic_crypto.hpp         <- sha256/hmac/aes-gcm/base64
    lic_hwid.hpp           <- WMI-tabanlı HWID
    lic_antidebug.hpp      <- polimorfik tehdit puanı
    lic_ws.hpp             <- WinHTTP WebSocket (ws:// / wss://)
    lic_tcp.hpp            <- Ham TCP (tcp://), sunucu ile aynı çerçeve
    lic_login_ui.hpp       <- ImGui+DX9 manuel login penceresi (modal)
  src/
    lic_crypto.cpp
    lic_hwid.cpp
    lic_antidebug.cpp
    lic_ws.cpp
    lic_tcp.cpp
    lic_login_ui.cpp
    LicenseGuard.cpp       <- orchestrator (state machine)
  tools/
    pack-license.mjs       <- license.dat üreticisi (Node.js, opsiyonel)
```

## Entegrasyon

Bu projeye `Valorant.vcxproj` ve `Valorant.vcxproj.filters` zaten güncellendi.
Yeni bir Visual Studio projesinde aynısını yapmak için:

1. `LicenseGuard/` klasörünü projenin altına kopyala
2. Solution Explorer'a sürükle (`*.cpp` dosyaları "Source Files" altına gelmeli)
3. **Project → Properties → Linker → Input → Additional Dependencies**'e ekle:
   ```
   bcrypt.lib;wbemuuid.lib;ole32.lib;oleaut32.lib;iphlpapi.lib;psapi.lib;winhttp.lib;ws2_32.lib;shlwapi.lib
   ```
4. main.cpp'de **(önerilen: manuel login modu)**:
   ```cpp
   #include "LicenseGuard/include/LicenseGuard.hpp"
   #include "LicenseGuard/include/lic_xor.hpp"

   int main() {
       lic::StartOptions opts;
       opts.client_version = "1.0.0";
       opts.show_messagebox_on_kick = true;
       opts.exit_process_on_kick = true;

       // Manuel login: license.dat yoksa ImGui penceresi aç
       opts.manual_input_if_missing = true;
       opts.save_after_manual_login  = true;     // başarılı login sonrası kaydet
       opts.product_display_name     = "Expectional";

       // Bunlar binary'ye gömülür (LXS = compile-time XOR obfuscation)
       opts.baked_app_id  = LXS("demo-app");
       opts.baked_ws_url  = LXS("tcp://localhost:3001");

       if (!lic::Start(opts)) return 1;       // AUTH_OK alınana kadar blokluyor
       // ... senin programın ...
   }
   ```

## Akış: ilk açılış vs. sonraki açılışlar

**İlk açılış (license.dat yok):**
1. `lic::Start()` çağrılır
2. license.dat bulunmaz
3. ImGui login penceresi açılır (480×340, ekran ortası, modern dark tema)
4. Kullanıcı lisans key girer → "Login" butonu
5. Arka plan worker thread auth dener; UI'da "Authenticating..."
6. **Başarılı**: pencere kapanır, license.dat yazılır, `Start()` true döner
7. **Başarısız**: pencere açık kalır, hata mesajı görünür (örn. "license_invalid", "hwid_mismatch"), kullanıcı tekrar dener

**Sonraki açılışlar (license.dat var):**
1. `lic::Start()` license.dat'ı okur
2. Doğrudan auth dener
3. **Başarılı**: hiçbir UI yok, sessizce başlar
4. **Başarısız** (lisans iptal edildi, HWID değişti vb.): license.dat silinir, manuel login penceresi açılır

Yani kullanıcı bir kez key girer, sonraki açılışlarda otomatik. Lisans
iptal edilirse bir sonraki açılışta tekrar sorulur.

## (Opsiyonel) license.dat'ı dışarıdan üretmek

Manuel login modunda buna **gerek yok**. Ama önceden hazır bir license.dat
dağıtmak istersen (kullanıcı hiç input görmesin):

```powershell
cd Valorant\LicenseGuard\tools
node pack-license.mjs `
  --key "DEMO1-DEMO2-DEMO3-DEMO4" `
  --out "..\..\x64\Release\license.dat"
```

Üretim için:
- İstemci `baked_ws_url` üretimde `tcp://host:port` veya `wss://host/ws` olmalı (sunucu yapılandırmasıyla aynı)
- `--key` her kullanıcıya farklı verilmeli (dashboard'dan üretilir)
- Yani **her kullanıcı için ayrı license.dat** dağıtılır

`license.dat` exe ile aynı klasörde olmalı.

> **Not:** `license.dat` yalnızca lisans anahtarını içerir; sunucu adresi
> `baked_ws_url` ile binary'de tutarlı olmalı; yoksa bağlantı başarısız → manuel login.

## Protokol akışı

1. `lic::Start()` → config (license.dat veya manuel login UI) → HWID hesapla → TCP veya WS bağlantısı
2. Server → `CHALLENGE` (nonce)
3. Client → `HANDSHAKE` (app_id, hwid, code_hash)
4. Client → `AUTH` (license_key, HMAC-SHA256(UPPER(key), "appId|nonce|hwid|ts"))
5. Server → `AUTH_OK` (session_token JWT) → `Start()` true döner
6. Heartbeat döngüsü her 25 sn:
   - Client → `HEARTBEAT` (token, integrity = HMAC(token, "jti|seq|rdtsc^threat|code_hash"))
   - Server → `HB_ACK` (next_token = rotated)
7. Server `KICK` → MessageBox + ExitProcess(1) (manuel login denemesi sırasında bu davranış geçici devre dışı)

## Anti-tamper özellikleri

| Katman | Detay |
|--------|-------|
| String obf | LXS macro ile derleme zamanı XOR (`strings`'e direnç) |
| HWID | CPU + Disk + Motherboard + Machine GUID; SHA-256 |
| HWID drift | 4 bileşenden 60% match yeterli (ağırlıklı puan) |
| Anti-debug | PEB.BeingDebugged, NtGlobalFlag, timing detour, CheckRemoteDebugger, hypervisor bit, debugger process names |
| Code integrity | Module ilk 1 MB SHA-256, her HB'de kontrol |
| Token rotation | Her HB'de yeni JWT (replay deyince eski geçersiz) |
| Network sırrı | Master HMAC binary'de YOK; key = UPPER(license_key) |
| Server cevap | Tek lisans cracklenirse sadece o revoke; sistem ayakta |

## BUILD_SECRET rotasyonu

`lic_xor.hpp` ve `pack-license.mjs` aynı `BUILD_SECRET`/`LABEL`'ı kullanır.
Her major sürümde:
1. `LicenseGuard.cpp` içinde `LXS("expectional-license-guard-v1")` → yeni string
2. `pack-license.mjs` içinde `BUILD_SECRET` → aynı yeni string
3. Tüm kullanıcılar için yeni `license.dat` üret + dağıt

Bu sayede eski `license.dat`'lar yeni binary ile çalışmaz.

## Server tarafı

Bu modül `server/` ile çalışır. Aynı protokol:
- `CHALLENGE/HANDSHAKE/AUTH/HEARTBEAT/HB_ACK/KICK/BLOB_GET/BLOB_RESP` (dosya id'si de `BLOB_GET.blob_key` ile istenebilir), isteğe bağlı `DOWNLOAD_RESP` / `FILE_DOWNLOAD_RESP`
- `licenseKey.toUpperCase()` HMAC anahtarı
- Server tarafında lisans `SHA-256(UPPER(key))` ile bulunur

Ayrıntı: repo kök dizinindeki `SECURITY.md`.
