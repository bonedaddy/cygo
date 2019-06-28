package main

import (
	"fmt"
	"go/ast"
	"go/token"
	"go/types"
	"strings"
)

// compile line context
type compContext struct {
	le  ast.Expr
	re  ast.Expr
	lty types.Type
	rty types.Type
}

func ismapty(tystr string) bool      { return strings.HasPrefix(tystr, "map[") }
func ismapty2(typ types.Type) bool   { return ismapty(typ.String()) }
func isstrty(tystr string) bool      { return tystr == "string" }
func isstrty2(typ types.Type) bool   { return isstrty(typ.String()) }
func isslicety(tystr string) bool    { return strings.HasPrefix(tystr, "[]") }
func isslicety2(typ types.Type) bool { return isslicety(typ.String()) }

func newLitInt(v int) *ast.BasicLit {
	return &ast.BasicLit{Kind: token.INT, Value: fmt.Sprintf("%d", v)}
}
func newLitStr(v string) *ast.BasicLit {
	return &ast.BasicLit{Kind: token.STRING, Value: fmt.Sprintf("%s", v)}
}
func newLitFloat(v float32) *ast.BasicLit {
	return &ast.BasicLit{Kind: token.FLOAT, Value: fmt.Sprintf("%f", v)}
}