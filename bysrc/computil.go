package main

import (
	"fmt"
	"go/ast"
	"go/token"
	"go/types"
	"log"
	"reflect"
	"strings"
	"unicode"

	"gopp"
)

// compile line context
type compContext struct {
	le  ast.Expr
	re  ast.Expr
	lty types.Type
	rty types.Type
}

func ismapty(tystr string) bool    { return strings.HasPrefix(tystr, "map[") }
func ismapty2(typ types.Type) bool { return ismapty(typ.String()) }
func isstrty(tystr string) bool {
	if strings.HasPrefix(tystr, "untyped ") {
		tystr = tystr[8:]
	}
	return tystr == "string"
}
func isstrty2(typ types.Type) bool {
	if typ == nil {
		log.Println("todo", typ)
		return false
	}
	return isstrty(typ.String())
}
func isslicety(tystr string) bool    { return strings.HasPrefix(tystr, "[]") }
func isslicety2(typ types.Type) bool { return isslicety(typ.String()) }
func isarrayty(tystr string) bool {
	s := ""
	for _, c := range tystr {
		if !unicode.IsDigit(c) {
			s += string(c)
		}
	}
	return strings.HasPrefix(s, "[]") && !strings.HasPrefix(tystr, "[]")
}
func isarrayty2(typ types.Type) bool   { return isarrayty(typ.String()) }
func iseface(tystr string) bool        { return strings.HasPrefix(tystr, "interface{}") }
func iseface2(typ types.Type) bool     { return typ != nil && iseface(typ.String()) }
func istypety(tystr string) bool       { return strings.HasPrefix(tystr, "type ") }
func istypety2(typ types.Type) bool    { return istypety(typ.String()) }
func ischanty(tystr string) bool       { return strings.HasPrefix(tystr, "chan ") }
func ischanty2(typ types.Type) bool    { return ischanty(typ.String()) }
func isvarty(tystr string) bool        { return strings.HasPrefix(tystr, "var ") }
func isvarty2(typ types.Type) bool     { return isvarty(typ.String()) }
func isstructty(tystr string) bool     { return strings.Contains(tystr, "/.") } // struct ???
func isstructty2(typ types.Type) bool  { return isstructty(typ.String()) }
func isinvalidty(tystr string) bool    { return strings.HasPrefix(tystr, "invalid ") }
func isinvalidty2(typ types.Type) bool { return isinvalidty(typ.String()) }

func newLitInt(v int) *ast.BasicLit {
	return &ast.BasicLit{Kind: token.INT, Value: fmt.Sprintf("%d", v)}
}
func newLitStr(v string) *ast.BasicLit {
	return &ast.BasicLit{Kind: token.STRING, Value: fmt.Sprintf("%s", v)}
}
func newLitFloat(v float32) *ast.BasicLit {
	return &ast.BasicLit{Kind: token.FLOAT, Value: fmt.Sprintf("%f", v)}
}
func newIdent(v string) *ast.Ident {
	idt := &ast.Ident{}
	idt.Name = v
	return idt
}

func typesty2str(typ types.Type) string {
	ret := ""
	switch aty := typ.(type) {
	case *types.Basic:
		ret = fmt.Sprintf("%v", typ)
		ret = strings.Replace(ret, ".", "_", 1) // unsafe.Pointer
	case *types.Interface:
		return gopp.IfElseStr(aty.NumMethods() > 0, "cxiface", "cxeface")
	default:
		gopp.G_USED(aty)
		log.Println("todo", typ, reflect.TypeOf(typ))
		ret = fmt.Sprintf("%v", typ)
	}
	return ret
}

func iscsel(e ast.Expr) bool {
	idt, ok := e.(*ast.Ident)
	if ok {
		return idt.Name == "C"
	}
	return false
}

func sign2rety(v string) string {
	segs := strings.Split(v, " ")
	retstr := segs[len(segs)-1]
	isptr := retstr[0] == '*'
	pos := strings.LastIndex(retstr, "/.")
	if pos > 0 {
		retstr = retstr[pos+2:]
	}
	if isstrty(retstr) {
		return "cxstring*"
	}
	if iseface(retstr) {
		retstr = "cxeface"
	}
	if retstr == "unsafe.Pointer" {
		return "unsafe_Pointer"
	}
	retstr = strings.TrimLeft(retstr, "*")
	return gopp.IfElseStr(isptr, retstr+"*", retstr)
}

var tmpvarno = 100

func tmpvarname() string {
	tmpvarno++
	return fmt.Sprintf("gxtv%d", tmpvarno)
}

// idt is ast.CallExpr.Fun
func funcistypedep(idt ast.Expr) bool {
	switch te := idt.(type) {
	case *ast.Ident:
		switch te.Name {
		case "string":
			return true
		default:
			log.Println("todo", idt, reflect.TypeOf(idt))
		}
	case *ast.SelectorExpr:
		if fmt.Sprintf("%v", te.X) == "unsafe" && fmt.Sprintf("%v", te.Sel) == "Pointer" {
			return true
		}
	default:
		log.Println("todo", idt, reflect.TypeOf(idt))
	}
	return false
}

type basecomp struct {
	psctx    *ParserContext
	strtypes map[string]types.TypeAndValue
}

func newbasecomp(psctx *ParserContext) *basecomp {
	bc := &basecomp{strtypes: map[string]types.TypeAndValue{}}
	bc.psctx = psctx
	bc.initbc()
	return bc
}
func (bc *basecomp) initbc() {
	psctx := bc.psctx
	for tye, tyval := range psctx.info.Types {
		bc.strtypes[bc.exprstr(tye)] = tyval
	}
}

// idt is ast.CallExpr.Fun
func (bc *basecomp) funcistype(idt ast.Expr) bool {
	tyval, ok := bc.strtypes[bc.exprstr(idt)]
	if ok {
	}
	return ok && tyval.IsType()
}

func (bc *basecomp) exprstr(e ast.Expr) string { return types.ExprString(e) }

func (c *basecomp) exprpos(e ast.Node) token.Position {
	if e == nil {
		return token.Position{}
	}
	return c.psctx.fset.Position(e.Pos())
}
