package xstrings

/*
 */
import "C"

type Builder struct {
	data []byte
}

func NewBuilder() *Builder {
	sb := &Builder{}
	return sb
}

func (sb *Builder) Write(s string) {
	sb.data.appendn(s.ptr, s.len)
	// C.cxarray2_appendn(sb.data, s, len(s))
}

func (sb *Builder) Write2() {

}

func (sb *Builder) Len() int {
	return len(sb.data)
}

func (sb *Builder) String() string {
	blen := len(sb.data)
	// s := string(sb.data) // not work
	s := gostringn(sb, blen)
	return s
}
