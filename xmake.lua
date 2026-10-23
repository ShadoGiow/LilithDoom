set_xmakever("3.0.0")

-- CommonLibSSE-NG (versione fissata), clonata dal workflow in lib/commonlibsse-ng
includes("lib/commonlibsse-ng")

set_project("BladeFireMode")
set_version("1.0.0")
set_languages("c++23")
set_license("MIT")

add_rules("mode.debug", "mode.releasedbg")

target("BladeFireMode")
    add_rules("commonlibsse-ng.plugin", {
        name = "BladeFireMode",
        author = "Giovanni",
        description = "Lama in fiamme con accensione graduale in sync con un'animazione (uso personale)"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_syslinks("user32", "kernel32")
    add_cxxflags("cl::/wd4100", "cl::/wd4201", "cl::/utf-8", {force = true})

    after_build(function (target)
        local dist = path.join(os.projectdir(), "dist", "SKSE", "Plugins")
        os.mkdir(dist)
        os.cp(target:targetfile(), dist)
        local pdb = target:symbolfile()
        if pdb and os.isfile(pdb) then
            os.cp(pdb, dist)
        end
    end)
