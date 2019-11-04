## ArcDPS GW2Clipboard plug-in

An ArcDPS plug-in to integrate [GW2Clipboard](https://github.com/maklorgw2/gw2clipboard) with [ArcDPS](https://www.deltaconnected.com/arcdps/).

**GW2Clipboard version 1.1 or above is required**

This simple plugin opens gw2clipboard, or if already opened restores the window if applicable when Guild Wars 2 is opened..

When Guild Wars 2 is closed, this plugin notifies gw2clipboard to minimize to the system tray.

### Installation

Download the current dll from the [Releases](https://github.com/maklorgw2/arcdps_gw2clipboard/releases) page and copy this file into the **bin64** directory under your Guild Wars 2 folder (For example: *C:\Program Files (x86)\Guild Wars 2\bin64*)

A configuration file is required to tell this plug-in where gw2clipboard has been installed. 
* This configuration folder and file will need to be created in the **addons** folder under your Guild Wars 2 folder (For example: *C:\Program Files (x86)\Guild Wars 2\addons*)
* The required folder and file will be automatically created the first time the plugin is loaded with ArcDPS. Please ensure the addons folder has write permissions. If this fails for any reason you can do this step manually by:
  - Create a folder named **gw2clipboard** inside the **addons** folder (For example: *C:\Program Files (x86)\Guild Wars 2\addons\gw2clipboard*)
  - Create a file named **gw2clipboard.ini**

The configuration file needs to contain the following line (**please use the path where you have installed GW2Clipboard if this default path is incorrect**)
```sh
PATH=C:\Program Files (x86)\Guild Wars 2\addons\gw2clipboard
```

The next time you run Guild Wars 2, if configured correctly GW2Clipboard should be automatically run.

### Compilation notes

If you decide to build this DLL from source, there are two x64 configurations:
* Debug - This version opens a console and shows some limited debug information
* Release - This is the version you should be installing 

