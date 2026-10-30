$f='C:/Users/jonsu/dev/framelink-m4-20261030';$env:PATH="C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;$env:PATH"
ctest --test-dir "$f/build-final" -C Release -R '^windows_endpoint_ready$' --repeat until-fail:5 --output-on-failure
if($LASTEXITCODE){exit $LASTEXITCODE}
ctest --test-dir "$f/build-final" -C Release -R '^gst_timestamp_contract$' --repeat until-fail:100 --output-on-failure
if($LASTEXITCODE){exit $LASTEXITCODE}
ctest --test-dir "$f/build-final" -C Release --output-on-failure
exit $LASTEXITCODE
