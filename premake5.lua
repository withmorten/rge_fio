workspace "rge_fio"
	configurations { "Release", "Debug" }
	location "build"

	files { "zlib/*.*"}

	files { "src/*.*" }

	includedirs { "zlib"}
	includedirs { "src" }

project "rge_fio"
	kind "ConsoleApp"
	language "C"
	targetname "rge_fio"
	targetdir "bin/%{cfg.buildcfg}"

	configuration { "linux", "gmake" }
		linkoptions { '-static-libstdc++', '-static-libgcc' }
		entrypoint ("main")

	defines { "Z_SOLO" }

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "full"
		optimize "off"
		runtime "debug"
		editAndContinue "off"
		flags { "NoIncrementalLink" }
		staticruntime "on"

	filter "configurations:Release"
		defines { "NDEBUG" }
		symbols "on"
		optimize "speed"
		runtime "release"
		staticruntime "on"
		flags { "LinkTimeOptimization" }
