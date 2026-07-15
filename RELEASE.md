# Release policy

`eng/Build-Release.ps1`はself-contained単一EXE、設定、ユーザー単位installer/uninstaller、SHA-256一覧、ZIPを`artifacts/release`へ生成します。通常ユーザーでインストールでき、管理者権限・ゲームprocessへのinject・driverは不要です。

```powershell
.\eng\Build-Release.ps1 -Runtime win-x64
```

コード署名時はWindows証明書storeにあるAuthenticode証明書のthumbprintを`SPECTATOR_SIGN_CERT_SHA1`へ設定します。証明書・private key・passwordはrepositoryやartifactへ格納しません。署名時はDigiCert timestampを付与します。

自動更新は、署名済みreleaseとHTTPS配信基盤が用意されるまでアプリから無断適用しません。配布時はversion、ZIP URL、SHA-256、公開日時を持つHTTPS manifestをVolputas管理下で公開し、Spectatorは通知のみ、ユーザー操作で署名済みinstallerを起動する方針です。downgrade、未署名binary、hash不一致は拒否します。
