Step-by-step installation (contributed by Leif from HDSDR)

* install HDSDR from http://www.hdsdr.de/download/HDSDR_install.exe

* download Zadig from http://zadig.akeo.ie
  Windows Vista/7/8: download the newest version - Windows XP users require a special zadig_xp version!

* install and start Zadig: press "Install Driver" to install the WinUSB drivers after selecting the right device(s).
Problems occur when the original driver (delivered with the HackRF One) is still installed!
These drivers sometimes do not get uninstalled correctly.
In this case, click Options and enable "List All Devices", then choose the HackRF One and press "Replace Driver".

* download ExtIO_HackRF.DLL https://github.com/jocover/ExtIO_HackRF/releases

* copy downloaded file into your HDSDR installation directory (default=C:\Program Files (x86)\HDSDR)

* (re)start HDSDR (select ExtIO_HackRF.DLL if demanded)
