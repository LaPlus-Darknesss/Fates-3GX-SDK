# Getting started with Fates-3GX SDK

	This page walks you through:
		
		1. What this SDK actually is
		
		2. How to build the plugin from source
		
		3. How to install it on hardware/emulator
		
		4. How to confirm that it's working
		
## 1. What this SDK is (in plain language)

	- **Fire Emblem Fates** is a 3DS game compiled to ARM code.
	
	- The game does not understand any other language
	
	- A **3GX plugin** is a small plugin the system loads *alongside* the game.
	
	- The SDK provides:
		
		- A **plugin** that injects itself into Fates's process
		
		- A set of **hooks** into important game functions (damage, HP, RNG, map start/end, etc.)
		
		- A small **engine** (event bus + context structs) that turns those hooks into friendly C++ callbacks you can register from your own code.
		
	**So instead of:**
	
		> "Rewrite the game's assembly to change how damage works"
		
	**You do:**
	
		---
			
			void MyModule_OnHpChange(const HpChangeContext &ctx)
			{
				// inspect or modify ctx here
			}	

			bool MyModule_RegisterHandlers()
			{
				return Fates::Engine::RegisterHpChangeHandler(&MyModule_OnHpChange);
			}
			
		---
	
	**The SDK takes care of:**
		
		Finding the right functions in code.bin (for the specific game version you are using)
		
		Patching safely
		
		Calling Engine::On* stubs
		
		Fanning those events out through the **event bus** to your handlers.
		
## 2. Repository layout (high level)
		
		Some directories you will often interact with:
			
			plugin/
			
				src/ - C++ source code for the 3GX plugin
				include/ - headers for the plugin and engine 
				
			plugin/src/engine/
				
				Engine internals and example modules
				
			plugin/include/engine
			
				Public engine headers
				
			addresses/
				
				Version-specific addresses (NOT INCLUDED IN PUBLIC RELASE, ALL ADDRESSES ARE SPECIFICALLY FOR NA Fates Special Edition code.bin)
				
			docs/
				
				Documentation (this file, engine examples, reference pages, etc.)
				
			build.py
				
				Python build script that drives devkitARM and produces the .3gx file

				/original/
					
					Place your decrypted NA v1.1 Special Edition code.bin here.
  					This file is not committed to git.

				/build/
  					
					Output directory for the compiled plugin (plugin_ctrpf.3gx, .elf, .map, etc.).
				
		
		You don’t need to understand every folder to begin.
		
		For a first experiment, you really only need plugin/src/engine and plugin/src/main.cpp.
		
## 3. Prerequisites

	To build the plugin from source you’ll need:
	
		Python 3
		
		devkitPro + devkitARM installed and on your **PATH**
		
		A way to run CTRPF/3GX plugins:
				
				On hardware: a CFW setup that can load .3gx plugins for 3DS titles
				
				On emulator: An emulator build that supports CTRPF/3GX plugins (My reccomendation is Azahar)
		
		You will also need a Fire Emblem Fates Special Edition (NA) dump matching the addresses this SDK targets (NA v1.1). Other regions/versions are not currently supported.
		
		You will need to configure build_config.toml to match your directories  
		
## 4. From the root of the repo, run:

	---
	
	py build.py --config build_config.toml
	
	---
	
	If everything is set up correctly, you should see a build log and end up with a .3gx file under a build/ or out/ directory (the exact path/name depends on your build_config.toml).
	
	### 3GX tool

		By default, the build scripts use the bundled `tools/3gxtool/3gxtool.exe`
		(3DS Game eXtension Tool v1.3) to generate the final `.3gx` plugin.

		If you prefer to use a system-wide installation of 3gxtool instead, you can put
		it in your `PATH` and adjust the build configuration accordingly.
		
		From my testing, other versions do not compile correctly, so please use the provided one.

	
	**Common things to check if this fails:**
	
		devkitARM is installed and arm-none-eabi-g++ is on your **PATH**
		
		Python can see all required modules 
		
		build_config.toml points at the correct plugin anme and output paths.
		
## 5. Installing the plugin
		
		The exact directory depends on your setup and region, but in general:
		
			1. Locate the **Title ID** for **Fire Emblem Fates Special Edition (NA)** in your plugin directory, example:
				
				sd:/plugins/<FATES_TITLE_ID>/
				
			2. Copy the built .3gx file from the build step in that folder.
			
			
			3. Ensure your CFW / Emulator configuration is set up to **load plugins** for that title. 
			

## 6. Verifying that it works:
		
			1. Launch the game with the plugin enabled
		
			2. Start a map/battle
		
		If everything is wired correctly, you should see a Fates3GX folder that contains your logs in your SD/SDMC directory (E.G. AppData\Roaming\Azahar\sdmc\Fates3GX\fates_3gx.log)
		
		
