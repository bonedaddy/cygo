module github.com/pwaller/go2ll

go 1.12

require (
	github.com/llir/llvm v0.3.0-pre6.0.20190209230502-10a64dac8a1a
	golang.org/x/tools v0.0.0-20190208222737-3744606dbb67
)

replace github.com/llir/llvm => ./llvm
