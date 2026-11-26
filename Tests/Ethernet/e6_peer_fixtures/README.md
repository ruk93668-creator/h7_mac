# Peer parser QVGA fixture

qvga.jpg is a test-only baseline JPEG. It is not compiled into any firmware or included in the original eight E6 fixtures.

Source: ../e6_jpeg_fixtures/frame0.jpg, resized to 320x240 using Pillow 12.2.0 NEAREST,
saved as JPEG quality=85, progressive=False. Pillow decoded the resulting file successfully.
The tests read the committed JPEG bytes and require no Pillow runtime dependency.

Source SHA-256: ba5c8f6cea8743414f2c341967f43d30d54261a7e90ff7fecace229c70f65978
QVGA SHA-256: fcd034a287bf10d80c775c0edfea9093cf476e0469c7e2b6117a0d746b9959bc
