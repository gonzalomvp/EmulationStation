//EmulationStation, a graphical front-end for ROM browsing. Created by Alec "Aloshi" Lofquist.
//http://www.aloshi.com

#include <SDL.h>
#include <iostream>
#include <iomanip>
#include "Renderer.h"
#include "views/ViewController.h"
#include "SystemData.h"
#include <boost/filesystem.hpp>
#include "guis/GuiDetectDevice.h"
#include "guis/GuiMsgBox.h"
#include "AudioManager.h"
#include "platform.h"
#include "Log.h"
#include "Window.h"
#include "EmulationStation.h"
#include "Settings.h"
#include "ScraperCmdLine.h"
#include <sstream>
#include <boost/locale.hpp>

#include <libcec/cec.h>
#include <libcec/cecloader.h>

#include "bcm_host.h"
//#LINKFLAGS=-lbcm_host

#include <algorithm>
#include <map>

#ifdef WIN32
#include <Windows.h>
#endif

namespace fs = boost::filesystem;
std::map<std::string, int> key2cec;
std::map<int, int> cec2sdl;

bool scrape_cmdline = false;

bool parseArgs(int argc, char* argv[], unsigned int* width, unsigned int* height)
{
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--resolution") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid resolution supplied.";
				return false;
			}

			*width = atoi(argv[i + 1]);
			*height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
		}else if(strcmp(argv[i], "--gamelist-only") == 0)
		{
			Settings::getInstance()->setBool("ParseGamelistOnly", true);
		}else if(strcmp(argv[i], "--ignore-gamelist") == 0)
		{
			Settings::getInstance()->setBool("IgnoreGamelist", true);
		}else if(strcmp(argv[i], "--draw-framerate") == 0)
		{
			Settings::getInstance()->setBool("DrawFramerate", true);
		}else if(strcmp(argv[i], "--no-exit") == 0)
		{
			Settings::getInstance()->setBool("ShowExit", false);
		}else if(strcmp(argv[i], "--debug") == 0)
		{
			Settings::getInstance()->setBool("Debug", true);
			Settings::getInstance()->setBool("HideConsole", false);
			Log::setReportingLevel(LogDebug);
		}else if(strcmp(argv[i], "--windowed") == 0)
		{
			Settings::getInstance()->setBool("Windowed", true);
		}else if(strcmp(argv[i], "--vsync") == 0)
		{
			bool vsync = (strcmp(argv[i + 1], "on") == 0 || strcmp(argv[i + 1], "1") == 0) ? true : false;
			Settings::getInstance()->setBool("VSync", vsync);
			i++; // skip vsync value
		}else if(strcmp(argv[i], "--scrape") == 0)
		{
			scrape_cmdline = true;
		}else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
#ifdef WIN32
			// This is a bit of a hack, but otherwise output will go to nowhere
			// when the application is compiled with the "WINDOWS" subsystem (which we usually are).
			// If you're an experienced Windows programmer and know how to do this
			// the right way, please submit a pull request!
			AttachConsole(ATTACH_PARENT_PROCESS);
			freopen("CONOUT$", "wb", stdout);
#endif
			std::cout << 
				"EmulationStation, a graphical front-end for ROM browsing.\n"
				"Written by Alec \"Aloshi\" Lofquist.\n"
				"Version " << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING << "\n\n"
				"Command line arguments:\n"
				"--resolution [width] [height]	try and force a particular resolution\n"
				"--gamelist-only			skip automatic game search, only read from gamelist.xml\n"
				"--ignore-gamelist		ignore the gamelist (useful for troubleshooting)\n"
				"--draw-framerate		display the framerate\n"
				"--no-exit			don't show the exit option in the menu\n"
				"--debug				more logging, show console on Windows\n"
				"--scrape			scrape using command line interface\n"
				"--windowed			not fullscreen, should be used with --resolution\n"
				"--vsync [1/on or 0/off]		turn vsync on or off (default is on)\n"
				"--help, -h			summon a sentient, angry tuba\n\n"
				"More information available in README.md.\n";
			return false; //exit after printing help
		}
	}

	return true;
}

bool verifyHomeFolderExists()
{
	//make sure the config directory exists
	std::string home = getHomePath();
	std::string configDir = home + "/.emulationstation";
	if(!fs::exists(configDir))
	{
		std::cout << "Creating config directory \"" << configDir << "\"\n";
		fs::create_directory(configDir);
		if(!fs::exists(configDir))
		{
			std::cerr << "Config directory could not be created!\n";
			return false;
		}
	}

	return true;
}

// Returns true if everything is OK, 
bool loadSystemConfigFile(const char** errorString)
{
	*errorString = NULL;

	if(!SystemData::loadConfig())
	{
		LOG(LogError) << "Error while parsing systems configuration file!";
		*errorString = "IT LOOKS LIKE YOUR SYSTEMS CONFIGURATION FILE HAS NOT BEEN SET UP OR IS INVALID. YOU'LL NEED TO DO THIS BY HAND, UNFORTUNATELY.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.";
		return false;
	}

	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "No systems found! Does at least one system have a game present? (check that extensions match!)\n(Also, make sure you've updated your es_systems.cfg for XML!)";
		*errorString = "WE CAN'T FIND ANY SYSTEMS!\n"
			"CHECK THAT YOUR PATHS ARE CORRECT IN THE SYSTEMS CONFIGURATION FILE, "
			"AND YOUR GAME DIRECTORY HAS AT LEAST ONE GAME WITH THE CORRECT EXTENSION.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.";
		return false;
	}

	return true;
}

//called on exit, assuming we get far enough to have the log initialized
void onExit()
{
	Log::close();
}

//CEC::CBCecKeyPressType 
int on_keypress(void* not_used, const CEC::cec_keypress msg)
{
	if (msg.duration == 0)
	{
		if(cec2sdl.find(msg.keycode) == cec2sdl.end())
		{
			return 1;
		}
		
		int key_id = cec2sdl[msg.keycode];
		//std::cout << "on_keypress: " << static_cast<int>(msg.keycode) << " " << key_id << std::endl;
		
		SDL_Event* event = new SDL_Event();
		event->key.keysym.sym = key_id;
		event->type = SDL_KEYDOWN;
		SDL_PushEvent(event);
		event->type = SDL_KEYUP;
		SDL_PushEvent(event);
	}

	return 0;
}

bool loadCECFromXML()
{
	//Populate some defaults
	key2cec["a"] = CEC::CEC_USER_CONTROL_CODE_SELECT;
	key2cec["b"] = CEC::CEC_USER_CONTROL_CODE_EXIT;
	key2cec["down"] = CEC::CEC_USER_CONTROL_CODE_DOWN;
	key2cec["left"] = CEC::CEC_USER_CONTROL_CODE_LEFT;
	key2cec["right"] = CEC::CEC_USER_CONTROL_CODE_RIGHT;
	key2cec["up"] = CEC::CEC_USER_CONTROL_CODE_UP;
	key2cec["pageup"] = CEC::CEC_USER_CONTROL_CODE_F2_RED;
	key2cec["pagedown"] = CEC::CEC_USER_CONTROL_CODE_F3_GREEN;
	key2cec["start"] = CEC::CEC_USER_CONTROL_CODE_SETUP_MENU;
	key2cec["select"] = CEC::CEC_USER_CONTROL_CODE_F4_YELLOW;
	key2cec["exit"] = CEC::CEC_USER_CONTROL_CODE_CLEAR;
	
	cec2sdl[key2cec["a"]]=SDLK_RETURN;
	cec2sdl[key2cec["b"]]=SDLK_ESCAPE;
	cec2sdl[key2cec["down"]]=SDLK_DOWN;
	cec2sdl[key2cec["left"]]=SDLK_LEFT;
	cec2sdl[key2cec["right"]]=SDLK_RIGHT;
	cec2sdl[key2cec["up"]]=SDLK_UP;
	cec2sdl[key2cec["pageup"]]=SDLK_RIGHTBRACKET;
	cec2sdl[key2cec["pagedown"]]=SDLK_LEFTBRACKET;
	cec2sdl[key2cec["start"]]=SDLK_F1;
	cec2sdl[key2cec["select"]]=SDLK_F2;

	std::string path = InputManager::getConfigPath();
	if(!fs::exists(path))
		return false;
	
	pugi::xml_document doc;
	pugi::xml_parse_result res = doc.load_file(path.c_str());

	if(!res)
	{
		LOG(LogError) << "Error parsing input config: " << res.description();
		return false;
	}

	pugi::xml_node root = doc.child("inputList");
	if(!root)
		return false;

	pugi::xml_node node = root.find_child_by_attribute("inputConfig", "deviceName", "cec");
	if(node)
	{
		for(pugi::xml_node input = node.child("input"); input; input = input.next_sibling("input"))
		{
			std::string name = input.attribute("name").as_string();
			int id = input.attribute("id").as_int();
			key2cec[name] = id;
		}
		cec2sdl[key2cec["exit"]] = SDLK_F4;
	}

	node = root.find_child_by_attribute("inputConfig", "deviceName", "Keyboard");
	if(node)
	{
		for(pugi::xml_node input = node.child("input"); input; input = input.next_sibling("input"))
		{
			std::string name = input.attribute("name").as_string();
			int id = input.attribute("id").as_int();
			cec2sdl[key2cec[name]] = id;
		}
	}
	
	return true;
}

int main(int argc, char* argv[])
{
	unsigned int width = 0;
	unsigned int height = 0;

	std::locale::global(boost::locale::generator().generate(""));
	boost::filesystem::path::imbue(std::locale());

	if(!parseArgs(argc, argv, &width, &height))
		return 0;

	// only show the console on Windows if HideConsole is false
#ifdef WIN32
	// MSVC has a "SubSystem" option, with two primary options: "WINDOWS" and "CONSOLE".
	// In "WINDOWS" mode, no console is automatically created for us.  This is good, 
	// because we can choose to only create the console window if the user explicitly 
	// asks for it, preventing it from flashing open and then closing.
	// In "CONSOLE" mode, a console is always automatically created for us before we
	// enter main. In this case, we can only hide the console after the fact, which
	// will leave a brief flash.
	// TL;DR: You should compile ES under the "WINDOWS" subsystem.
	// I have no idea how this works with non-MSVC compilers.
	if(!Settings::getInstance()->getBool("HideConsole"))
	{
		// we want to show the console
		// if we're compiled in "CONSOLE" mode, this is already done.
		// if we're compiled in "WINDOWS" mode, no console is created for us automatically;
		// the user asked for one, so make one and then hook stdin/stdout/sterr up to it
		if(AllocConsole()) // should only pass in "WINDOWS" mode
		{
			freopen("CONIN$", "r", stdin);
			freopen("CONOUT$", "wb", stdout);
			freopen("CONOUT$", "wb", stderr);
		}
	}else{
		// we want to hide the console
		// if we're compiled with the "WINDOWS" subsystem, this is already done.
		// if we're compiled with the "CONSOLE" subsystem, a console is already created; 
		// it'll flash open, but we hide it nearly immediately
		if(GetConsoleWindow()) // should only pass in "CONSOLE" mode
			ShowWindow(GetConsoleWindow(), SW_HIDE);
	}
#endif

	//if ~/.emulationstation doesn't exist and cannot be created, bail
	if(!verifyHomeFolderExists())
		return 1;

	//start the logger
	Log::open();
	LOG(LogInfo) << "EmulationStation - v" << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING;

	//always close the log on exit
	atexit(&onExit);

	Window window;
	ViewController::init(&window);
	window.pushGui(ViewController::get());

	if(!scrape_cmdline)
	{
		if(!window.init(width, height))
		{
			LOG(LogError) << "Window failed to initialize!";
			return 1;
		}

		std::string glExts = (const char*)glGetString(GL_EXTENSIONS);
		LOG(LogInfo) << "Checking available OpenGL extensions...";
		LOG(LogInfo) << " ARB_texture_non_power_of_two: " << (glExts.find("ARB_texture_non_power_of_two") != std::string::npos ? "ok" : "MISSING");

		window.renderLoadingScreen();
	}

	const char* errorMsg = NULL;
	if(!loadSystemConfigFile(&errorMsg))
	{
		// something went terribly wrong
		if(errorMsg == NULL)
		{
			LOG(LogError) << "Unknown error occured while parsing system config file.";
			if(!scrape_cmdline)
				Renderer::deinit();
			return 1;
		}

		// we can't handle es_systems.cfg file problems inside ES itself, so display the error message then quit
		window.pushGui(new GuiMsgBox(&window,
			errorMsg,
			"QUIT", [] { 
				SDL_Event* quit = new SDL_Event();
				quit->type = SDL_QUIT;
				SDL_PushEvent(quit);
			}));
	}

	//run the command line scraper then quit
	if(scrape_cmdline)
	{
		return run_scraper_cmdline();
	}

	//dont generate joystick events while we're loading (hopefully fixes "automatically started emulator" bug)
	SDL_JoystickEventState(SDL_DISABLE);

	// preload what we can right away instead of waiting for the user to select it
	// this makes for no delays when accessing content, but a longer startup time
	ViewController::get()->preload();

	//choose which GUI to open depending on if an input configuration already exists
	if(errorMsg == NULL)
	{
		if(fs::exists(InputManager::getConfigPath()) && InputManager::getInstance()->getNumConfiguredDevices() > 0)
		{
			ViewController::get()->goToStart();
		}else{
			window.pushGui(new GuiDetectDevice(&window, true, [] { ViewController::get()->goToStart(); }));
		}
	}

	//CEC support
	
	// Initialise the graphics pipeline for the raspberry pi. Yes, this is necessary.
	bcm_host_init();

	// Set up the CEC config and specify the keypress callback function
	CEC::ICECCallbacks        cec_callbacks;
	CEC::libcec_configuration cec_config;
	cec_config.Clear();
	cec_callbacks.Clear();

	const std::string devicename("CECExample");
	devicename.copy(cec_config.strDeviceName, std::min(devicename.size(),13u) );
	
	cec_config.clientVersion       = CEC::LIBCEC_VERSION_CURRENT;
	cec_config.bActivateSource     = 1;
	cec_config.callbacks           = &cec_callbacks;
	cec_config.deviceTypes.Add(CEC::CEC_DEVICE_TYPE_RECORDING_DEVICE);

	cec_callbacks.CBCecKeyPress    = &on_keypress;

	// Get a cec adapter by initialising the cec library
	CEC::ICECAdapter* cec_adapter = LibCecInitialise(&cec_config);
	if( !cec_adapter )
	{ 
		std::cerr << "Failed loading libcec.so\n"; 
		return 1; 
	}

	// Try to automatically determine the CEC devices 
	CEC::cec_adapter devices[10];
	int8_t devices_found = cec_adapter->FindAdapters(devices, 10, NULL);
	if( devices_found <= 0)
	{
		std::cerr << "Could not automatically determine the cec adapter devices\n";
		UnloadLibCec(cec_adapter);
		return 1;
	}

	// Open a connection to the zeroth CEC device
	if( !cec_adapter->Open(devices[0].comm) )
	{
		std::cerr << "Failed to open the CEC device on port " << devices[0].comm << std::endl;
		UnloadLibCec(cec_adapter);
		return 1;
	}
	
	loadCECFromXML();
	
	//generate joystick events since we're done loading
	SDL_JoystickEventState(SDL_ENABLE);

	int lastTime = SDL_GetTicks();
	bool running = true;

	while(running)
	{
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_JOYHATMOTION:
				case SDL_JOYBUTTONDOWN:
				case SDL_JOYBUTTONUP:
				case SDL_KEYDOWN:
				case SDL_KEYUP:
				case SDL_JOYAXISMOTION:
				case SDL_TEXTINPUT:
				case SDL_TEXTEDITING:
				case SDL_JOYDEVICEADDED:
				case SDL_JOYDEVICEREMOVED:
					InputManager::getInstance()->parseEvent(event, &window);
					break;
				case SDL_QUIT:
					running = false;
					break;
			}
		}

		if(window.isSleeping())
		{
			lastTime = SDL_GetTicks();
			SDL_Delay(1); // this doesn't need to be accurate, we're just giving up our CPU time until something wakes us up
			continue;
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;
		lastTime = curTime;

		// cap deltaTime at 1000
		if(deltaTime > 1000 || deltaTime < 0)
			deltaTime = 1000;

		window.update(deltaTime);
		window.render();
		Renderer::swapBuffers();

		Log::flush();
	}
	
	// CEC Close down and cleanup
	cec_adapter->Close();
	UnloadLibCec(cec_adapter);

	while(window.peekGui() != ViewController::get())
		delete window.peekGui();
	window.deinit();

	SystemData::deleteSystems();

	LOG(LogInfo) << "EmulationStation cleanly shutting down.";

	return 0;
}
