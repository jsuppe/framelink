package dev.framelink.cross;
import android.os.Bundle;
import android.os.IBinder;
interface IProducer { void start(in Bundle lease, IBinder brokerOwner); }
