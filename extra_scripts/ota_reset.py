# OTA yukleme sonrasi USB varsa karti resetle (COM8)
Import("env")

def after_upload(source, target, env):
    import time
    upload_port = env.get("UPLOAD_PORT", "")
    if not upload_port or upload_port.startswith("COM") is False:
        # OTA IP ile yuklendi — cihaz ESP.restart() ile zaten yeniden baslar
        print("OTA upload bitti (cihaz otomatik reset)")
        time.sleep(2)
        return

    port = upload_port
    print("Upload sonrasi serial reset: " + port)
    time.sleep(1)
    env.Execute(
        '$PYTHONEXE "$UPLOAD_TOOLSCRIPT" --chip esp32s3 --port "' + port
        + '" --before default_reset --after hard_reset chip_id'
    )

env.AddPostAction("upload", after_upload)
