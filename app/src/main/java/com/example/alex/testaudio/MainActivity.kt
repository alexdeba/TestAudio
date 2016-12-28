package com.example.alex.testaudio

import android.os.Bundle
import android.widget.TextView
import android.support.v7.app.AppCompatActivity
import android.widget.Toast
import kotlinx.android.synthetic.main.content_main.*
import android.bluetooth.BluetoothAdapter


class MainActivity : AppCompatActivity() {

	lateinit var thread: Thread

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		setContentView(R.layout.activity_main)

		// init the UI

		btn_record.setOnClickListener { start_recording() }

		btn_stop.setOnClickListener { stop_recording() }

		vumeter.progress = 100


		// Example of a call to a native method
		val tv = findViewById(R.id.sample_text) as TextView
		tv.text = "test"

		init()
	}

	private fun init() {

/*

		// active le bluetooth si pas activé

		val mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
		if (!mBluetoothAdapter.isEnabled) {
			mBluetoothAdapter.enable()
		}

*/
		// initialise OpenSL
		// initialize native audio system

	}

	override fun onPause() {
		super.onPause()
	}

	override fun onDestroy() {
		super.onDestroy()
	}

	private fun start_recording() {

		val toast = Toast.makeText(this, "recording started", Toast.LENGTH_SHORT)
		toast.show()

		// create new file


		// launch the recording
		//val	created = createAudioRecorder()
		//if (created)
		//	startRecording()
		//startprocess()

		thread = object : Thread() {
			override fun run() {
				priority = Thread.MAX_PRIORITY
				startprocess()
			}
		}
		thread.start()

	}

	private fun stop_recording() {
		val toast = Toast.makeText(this, "recording stopped", Toast.LENGTH_SHORT)
		toast.show()

		// release resources

		stopprocess()
		try {
			thread.join()
		} catch (e: InterruptedException) {
			// TODO Auto-generated catch block
			e.printStackTrace()
		}

//		thread = null
	}


	/************************
	 *   méthodes natives   *
	 ************************/


	/**
	 * A native method that is implemented by the 'native-lib' native library,
	 * which is packaged with this application.
	 */

	external fun startprocess()

	external fun stopprocess()


	companion object {

		// Used to load the 'native-lib' library on application startup.
		init {
			System.loadLibrary("native-lib")
		}
	}

}
