$ErrorActionPreference='Stop'
$env:PATH='C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;'+$env:PATH
Set-Location '<win-home>/dev/framelink-m6-20261106'
New-Item -ItemType Directory -Path '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825' | Out-Null
$env:FRAMELINK_TRACE_EVENTS='65536'
$env:FRAMELINK_GST_TRACE_EVENTS='65536'
$env:FRAMELINK_TRACE='<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/consumer-core'
$env:FRAMELINK_GST_TRACE='<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/consumer-gst'
$consumerProcess=Start-Process -FilePath '<win-home>/dev/framelink-m6-20261106/build/Release/fl_gst_av_bench.exe' -ArgumentList '<win-home>/dev/framelink-m6-20261106/build/Release/gstframelink.dll --seconds 5 --size 3840x2160 --fps 30 --format BGRA --pool 2 --video-queue 1 --latency-ms 0 --arrival-budget-ms 34 --transport-only --no-output-preroll --output-queue-buffers 1 --raw --native --role consumer --session <win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/session --result <win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/consumer.txt' -WorkingDirectory '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825' -PassThru -RedirectStandardOutput '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/consumer.out' -RedirectStandardError '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/consumer.err'
Start-Sleep -Milliseconds 200
$env:FRAMELINK_TRACE='<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/producer-core'
$env:FRAMELINK_GST_TRACE='<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/producer-gst'
$producerProcess=Start-Process -FilePath '<win-home>/dev/framelink-m6-20261106/build/Release/fl_gst_av_bench.exe' -ArgumentList '<win-home>/dev/framelink-m6-20261106/build/Release/gstframelink.dll --seconds 5 --size 3840x2160 --fps 30 --format BGRA --pool 2 --video-queue 1 --latency-ms 0 --arrival-budget-ms 34 --transport-only --no-output-preroll --output-queue-buffers 1 --raw --native --role producer --session <win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/session --result <win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/producer.txt' -WorkingDirectory '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825' -PassThru -RedirectStandardOutput '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/producer.out' -RedirectStandardError '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/producer.err'
if (!$producerProcess.WaitForExit(120000)) { throw 'Producer timeout; inspect process before resuming' }
if (!$consumerProcess.WaitForExit(120000)) { throw 'Consumer timeout; inspect process before resuming' }
$producerProcess.Refresh()
$consumerProcess.Refresh()
@{producer_exit=$producerProcess.ExitCode;consumer_exit=$consumerProcess.ExitCode;config='--seconds 5 --size 3840x2160 --fps 30 --format BGRA --pool 2 --video-queue 1 --latency-ms 0 --arrival-budget-ms 34 --transport-only --no-output-preroll --output-queue-buffers 1 --raw --native'} | ConvertTo-Json | Set-Content '<win-home>/dev/framelink-m6-20261106/stage-BGRA-3840x2160-native-1794044825/exits.json'
