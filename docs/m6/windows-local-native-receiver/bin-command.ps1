$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-windows-6d7e6be'
ctest --test-dir build -C Release -R '^(gst_av_bin|gst_av_src_bin)$' --output-on-failure
exit $LASTEXITCODE