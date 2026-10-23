#pragma once

// Lettura dei tasti (pad XInput + tastiera/mouse) con un thread proprio.
// Serve a far funzionare le combinazioni di tasti anche senza hook sul gioco.

#include "Settings.h"

namespace bf::input
{
	void Start();
	void Stop();

	// eventi generati dalle combinazioni (consumati dal thread di gioco)
	int TakeOn();   // numero di pressioni "accendi" dall'ultima chiamata
	int TakeOff();  // numero di pressioni "spegni"

	// registrazione di una nuova combinazione dal menu
	enum class Target { None, On, Off };
	void   BeginCapture(Target a_target);
	void   CancelCapture();
	Target CaptureTarget();
	std::string CaptureLive();  // testo dei tasti premuti in questo momento

	// stato del pad (per mostrarlo nel menu)
	bool PadConnected();
}
