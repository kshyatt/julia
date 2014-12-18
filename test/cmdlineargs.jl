exename = joinpath(JULIA_HOME, (ccall(:jl_is_debugbuild, Cint, ()) == 0 ? "julia" : "julia-debug"))

# --version
let v = split(readall(`$exename -v`), "julia version ")[end]
    @test VERSION == VersionNumber(v)
end
@test readall(`$exename -v`) == readall(`$exename --version`)

# --eval
@test !success(`$exename -e "exit(1)"`)
@test !success(`$exename --eval="exit(1)"`)
@test !success(`$exename -e`)
@test !success(`$exename --eval`)

# --print
@test readall(`$exename -E "1+1"`) == "2\n"
@test readall(`$exename --print="1+1"`) == "2\n"
@test !success(`$exename -E`)
@test !success(`$exename --print`)

# --post-boot
@test success(`$exename -P "exit(0)"`)
@test success(`$exename --post-boot="exit(0)"`)
@test !success(`$exename -P`)
@test !success(`$exename --post-boot`)

# --load
let testfile = joinpath(dirname(@__FILE__), "test_loadfile.jl")
    @test split(readchomp(`$exename --load=$testfile -P "println(a)"`), '\n')[end] == "test"
    @test split(readchomp(`$exename -P "println(a)" -L $testfile`), '\n')[end] == "test"
    @test !success(`$exename -L`)
    @test !success(`$exename --load`)
end

# --procs
@test readchomp(`$exename -q -p 2 -P "println(nworkers()); exit(0)"`) == "2"
@test !success(`$exename -p 0`)
@test !success(`$exename --procs=1.0`)

# isinteractive
@test readchomp(`$exename -E "isinteractive()"`) == "false"
@test readchomp(`$exename -E "isinteractive()" -i`) == "true"

# --color
@test readchomp(`$exename --color=yes -E "Base.have_color"`) == "true"
@test readchomp(`$exename --color=no -E "Base.have_color"`) == "false"
@test !success(`$exename --color=false`)

# --int-literals
# readchomp(`$exename --int-literals=32 -E "sizeof(Int)"`)
# readchomp(`$exename --int-literals=64 -E "sizeof(Int)"`)

# --code-coverage
@test readchomp(`$exename -E "bool(Base.compileropts().code_coverage)"`) == "false"
@test readchomp(`$exename -E "bool(Base.compileropts().code_coverage)" --code-coverage=none`) == "false"

@test readchomp(`$exename -E "bool(Base.compileropts().code_coverage)" --code-coverage`) == "true"
@test readchomp(`$exename -E "bool(Base.compileropts().code_coverage)" --code-coverage=user`) == "true"

# --track-allocation
@test readchomp(`$exename -E "bool(Base.compileropts().malloc_log)"`) == "false"
@test readchomp(`$exename -E "bool(Base.compileropts().malloc_log)" --track-allocation=none`) == "false"

@test readchomp(`$exename -E "bool(Base.compileropts().malloc_log)" --track-allocation`) == "true"
@test readchomp(`$exename -E "bool(Base.compileropts().malloc_log)" --track-allocation=user`) == "true"

# --check-bounds
@test int(readchomp(`$exename -E "int(Base.compileropts().check_bounds)"`)) == 0
@test int(readchomp(`$exename -E "int(Base.compileropts().check_bounds)" --check-bounds=yes`)) == 1
@test int(readchomp(`$exename -E "int(Base.compileropts().check_bounds)" --check-bounds=no`)) == 2
@test !success(`$exename -E "exit(0)" --check-bounds=false`)

# --optimize
@test readchomp(`$exename -E "bool(Base.compileropts().opt_level)"`) == "false"
@test readchomp(`$exename -E "bool(Base.compileropts().opt_level)" -O`) == "true"
@test readchomp(`$exename -E "bool(Base.compileropts().opt_level)" --optimize`) == "true"

# --depwarn
@test readchomp(`$exename --depwarn=no -E "Base.syntax_deprecation_warnings(true)"`) == "false"
@test readchomp(`$exename --depwarn=yes -E "Base.syntax_deprecation_warnings(false)"`) == "true"
@test !success(`$exename --depwarn=false`)

# pass arguments
let testfile = joinpath(dirname(@__FILE__), "test_loadfile.jl")
    @test readchomp(`$exename $testfile foo -bar --baz`) ==  "UTF8String[\"foo\",\"-bar\",\"--baz\"]"
    @test !success(`$exename --foo $testfile`)
end
