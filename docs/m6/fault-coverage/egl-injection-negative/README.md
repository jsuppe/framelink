# Reusable EGL gate did not establish a GPU stall

Initial Linux test failed its lease-busy assertion. Instrumented retry showed exported sync-fd initiallynotready, thenready on iteration1 when collectionreturnedFL_OK. This was not proof of earlyFrameLinkrelease: nativefence itself hadsignalled. Independent rawEGLcontrol (before creating anyFrameLinkchannel) reproduced nativefence readiness behindanunsignalledreusablegate onNVIDIA RTX3090. A successful eglWaitSyncKHR return is insufficient evidence of effective native-fence ordering for this injection.

Added raw20ms fence-poll capability prerequisite, then signalgate andrequirecompletion. This host returns77/unavailable, notpass. Theoriginalfailedfixtureandnegativecontrol arepreserved. NoFrameLinkproductionchange orclaimedtransportbug. D3D11controlledqueue-delay remains separatelypassed. Androidcapability is checked independently. Linux/AndroiddelayedGPUnegativecoverage cannotbeclaimedfromordinarycompletion tests or eventfdcontrols.
