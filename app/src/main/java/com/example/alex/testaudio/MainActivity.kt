package com.example.alex.testaudio

import android.os.Bundle
import android.support.v7.app.AppCompatActivity
import android.widget.Toast
import kotlinx.android.synthetic.main.content_main.*
import android.bluetooth.BluetoothAdapter
import android.os.Build


class MainActivity : AppCompatActivity() {

	lateinit var thread: Thread
	var is_recording = false

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		setContentView(R.layout.activity_main)

		// init the UI

		btn_record.setOnClickListener { start_recording() }
		btn_stop.setOnClickListener { stop_recording() }
		vumeter.progress = 100

		init()
	}


	private fun init() {

//		// activate bluetooth if not active
//
//		if (!Build.FINGERPRINT.startsWith("generic")) { // bluetooth not supported on the emulator
//
//			val mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
//			if (!mBluetoothAdapter.isEnabled) {
//				mBluetoothAdapter.enable()
//			}
//		}
	}


	override fun onPause() {
		super.onPause()
	}


	override fun onDestroy() {
		if (is_recording)
			stop_recording()
		super.onDestroy()
	}


	private fun start_recording() {

		val toast = Toast.makeText(this, "recording started", Toast.LENGTH_SHORT)
		toast.show()

		thread = object : Thread() {
			override fun run() {
				priority = Thread.MAX_PRIORITY
				startprocess()
			}
		}
		thread.start()
		is_recording = true

	}


	private fun stop_recording() {
		val toast = Toast.makeText(this, "recording stopped", Toast.LENGTH_SHORT)
		toast.show()

		stopprocess()
		is_recording = false
		try {
			thread.join()
		} catch (e: InterruptedException) {
			// TODO Auto-generated catch block
			e.printStackTrace()
		}


	}


	companion object {

		// Used to load the 'native-lib' library on application startup.
		init {
			System.loadLibrary("native-lib")
		}
	}



	/**
	 * native methods implemented by the 'native-lib' native library
	 */

	external fun startprocess()
	external fun stopprocess()

}
