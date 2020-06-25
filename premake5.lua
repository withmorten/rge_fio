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

	defines { "Z_SOLO" }

	configuration { "gmake" }
		linkoptions { '-static-libstdc++', '-static-libgcc' }
		entrypoint ("main")

	configuration { "vs*" }
		characterset ("MBCS")
		toolset ("v141_xp")
		links { "legacy_stdio_definitions" }
		linkoptions { "/SAFESEH:NO" }
		defines { "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_DEPRECATE", "_USE_32BIT_TIME_T" }

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
