# Author: Phil Elwell <phil@raspberrypi.com>
# Copyright (c) 2018-2026, Raspberry Pi Ltd.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The names of the above-listed copyright holders may not be used
#    to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Device Tree engine: tokeniser, parser, in-memory node/property tree, and
# overlay merge/apply logic used by ovmerge.py. Split out into its own module
# so other tools (e.g. overlaycheck) can drive the same DT model in-process
# instead of shelling out to 'ovmerge' and parsing its text output.
# Fundamental types
# ''  - string
# '#' - 64-bit
# ':' - 32-bit integer
# ';' - 16-bit integer
# '.' - 8-bit integer (byte)
# '?' - boolean
# '!' - inverted boolean
# '[' - byte array     // Byte array syntax accepts but doesn't require colons between bytes
#   'prop['            // Interpret value as byte array and assign to prop
#   'prop[=00:01:02'   // property = literal byte array
#
# Operations on a fundamental type:
#                      // N.B. The type must go before the list so we know how to interpret it.
#
# 'reg:0'              // set reg and unit address to value
#   'reg:0=0'          // set reg and unit address to the supplied literal
#                      // The reg property is only set if it already exists.
#
# 'name'               // Assigning to name property automatically sets the node name
#                      // The name property is only set if it already exists.
#
# '=' - literal assignment (if a string, the literal after the =), if an integer,
#       either the in-band integer or the next cell (useful for phandles).
#   "prop=foo"         // String literal assignment
#   "prop=", &spi      // String path literal assignment (the path to the node is substituted, used for aliases)
#   "prop:0=0"         // Integer literal assignment
#   "prop:0=", <&spi>; // Integer cell assignment
#
# '{...}' - use the value as the key to an element in the set. The usual type indicators apply.
#   'prop{a='alpha',b='bravo',c='charlie'}";
#   'prop:0{0=",<&i2c0>,"1=",<&i2c1>,"3=0x2a}";

import os
import re
import sys
import subprocess

ELEM_SIZES = {
    '"': 0,  # string
    '.': 1,  # byte
    ';': 2,  # 16-bit int
    ':': 4,  # 32-bit int
    '#': 8,  # 64-bit int
}


class OvMergeError(Exception):
    pass


class State:
    def __init__(self):
        self.branch = None
        self.comment = False
        self.expand = False
        self.expand_label = False
        self.show_includes = False
        self.pi_extras = False
        self.sort = False
        self.warnings = False
        self.no_dts = False
        self.no_renumber = False
        self.cur_dt = None
        self.retcode = 0
        self.query = False
        self.force = False
        self.trace_parse = False
        self.trace_tree = False
        self.trace_prop = ''
        self.trace_label = ''
        self.bkpt = 0
        self.indent_str = "\t"


S = State()

# Node layout: [name, props, children, labels, parent, depth]
NAME, PROPS, CHILDREN, LABELS, PARENT, DEPTH = range(6)


def new_node(name):
    return [name, [], [], [], None, 0]


class DT:
    def __init__(self):
        self.root = None
        self.plugin = False
        self.labels = {}
        self.refcount = {}
        self.includes = []
        self.memreserves = []
        self.defines = {}
        self.frag_count = 0


class FileMarker:
    __slots__ = ('filename',)

    def __init__(self, filename):
        self.filename = filename


class PState:
    __slots__ = ('tokens', 'pos', 'file')

    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0
        self.file = None


# ---------------------------------------------------------------------------
# Small value helpers
# ---------------------------------------------------------------------------

def byte_array_value(value):
    arr = []
    for val in re.split(r'[: ]', value):
        if not re.fullmatch(r'([0-9a-fA-F][0-9a-fA-F])*', val):
            raise OvMergeError(f"* invalid bytestring at '{val}'")
        for i in range(0, len(val), 2):
            arr.append(int(val[i:i + 2], 16))
    return arr


def byte_array_string(arr):
    return ' '.join('%02x' % v for v in arr)


_EXPR_TOKEN_RE = re.compile(
    r"\s*(0[xX][0-9a-fA-F]+|0[bB][01]+|0[0-7]+|[0-9]+|<<|>>|[()+\-*/%&|^~])")


def _tokenize_expr(s):
    tokens = []
    i = 0
    n = len(s)
    while i < n:
        m = _EXPR_TOKEN_RE.match(s, i)
        if not m:
            if s[i:].strip() == '':
                break
            raise OvMergeError(f"* Bad integer value '{s}'")
        tokens.append(m.group(1))
        i = m.end()
    return tokens


def _parse_expr_literal(tok):
    if re.match(r'^0[xX]', tok):
        return int(tok, 16)
    if re.match(r'^0[bB]', tok):
        return int(tok, 2)
    if re.match(r'^0[0-7]+$', tok):
        return int(tok, 8)
    return int(tok, 10)


def _c_div(a, b):
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    return q


def _c_mod(a, b):
    return a - _c_div(a, b) * b


class _ExprParser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.i = 0

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def take(self):
        if self.i >= len(self.tokens):
            raise OvMergeError("* Incomplete expression")
        t = self.tokens[self.i]
        self.i += 1
        return t

    def parse(self):
        v = self.parse_or()
        if self.i != len(self.tokens):
            raise OvMergeError("* Trailing tokens in integer expression")
        return v

    def parse_or(self):
        v = self.parse_xor()
        while self.peek() == '|':
            self.take()
            v = v | self.parse_xor()
        return v

    def parse_xor(self):
        v = self.parse_and()
        while self.peek() == '^':
            self.take()
            v = v ^ self.parse_and()
        return v

    def parse_and(self):
        v = self.parse_shift()
        while self.peek() == '&':
            self.take()
            v = v & self.parse_shift()
        return v

    def parse_shift(self):
        v = self.parse_add()
        while self.peek() in ('<<', '>>'):
            op = self.take()
            r = self.parse_add()
            v = (v << r) if op == '<<' else (v >> r)
        return v

    def parse_add(self):
        v = self.parse_mul()
        while self.peek() in ('+', '-'):
            op = self.take()
            r = self.parse_mul()
            v = v + r if op == '+' else v - r
        return v

    def parse_mul(self):
        v = self.parse_unary()
        while self.peek() in ('*', '/', '%'):
            op = self.take()
            r = self.parse_unary()
            if op == '*':
                v = v * r
            elif op == '/':
                v = _c_div(v, r)
            else:
                v = _c_mod(v, r)
        return v

    def parse_unary(self):
        if self.peek() == '-':
            self.take()
            return -self.parse_unary()
        if self.peek() == '~':
            self.take()
            return ~self.parse_unary()
        if self.peek() == '+':
            self.take()
            return self.parse_unary()
        return self.parse_primary()

    def parse_primary(self):
        t = self.take()
        if t == '(':
            v = self.parse_or()
            if self.take() != ')':
                raise OvMergeError("* Mismatched parens in integer expression")
            return v
        return _parse_expr_literal(t)


def eval_expr(s):
    tokens = _tokenize_expr(s)
    if not tokens:
        raise OvMergeError(f"* Bad integer value '{s}'")
    return _ExprParser(tokens).parse()


# Preprocessor ('#if') expressions: C integer expressions extended with
# comparison and logical operators, 'defined' and macro expansion.

_PP_TOKEN_RE = re.compile(
    r"\s*(0[xX][0-9a-fA-F]+[uUlL]*|0[bB][01]+[uUlL]*|[0-9]+[uUlL]*"
    r"|[A-Za-z_]\w*|<<|>>|<=|>=|==|!=|&&|\|\||[()+\-*/%&|^~!<>])")
_IDENT_RE = re.compile(r'^[A-Za-z_]\w*$')


def _tokenize_pp_expr(s):
    tokens = []
    i = 0
    n = len(s)
    while i < n:
        m = _PP_TOKEN_RE.match(s, i)
        if not m:
            if s[i:].strip() == '':
                break
            raise OvMergeError(f"* Bad #if expression '{s.strip()}'")
        tok = m.group(1)
        if tok[0].isdigit():
            tok = re.sub(r'[uUlL]+$', '', tok)
        tokens.append(tok)
        i = m.end()
    return tokens


class _PPExprParser(_ExprParser):
    def __init__(self, tokens, defines, expanding):
        super().__init__(tokens)
        self.defines = defines
        self.expanding = expanding
        self.skip = 0  # >0 while evaluating a short-circuited operand

    def parse(self):
        if not self.tokens:
            raise OvMergeError("* #if with no expression")
        v = self.parse_lor()
        if self.i != len(self.tokens):
            raise OvMergeError(f"* Unexpected '{self.peek()}' in #if expression")
        return v

    def parse_lor(self):
        v = self.parse_land()
        while self.peek() == '||':
            self.take()
            self.skip += bool(v)
            r = self.parse_land()
            self.skip -= bool(v)
            v = int(bool(v) or bool(r))
        return v

    def parse_land(self):
        v = self.parse_or()
        while self.peek() == '&&':
            self.take()
            self.skip += not v
            r = self.parse_or()
            self.skip -= not v
            v = int(bool(v) and bool(r))
        return v

    def parse_and(self):
        v = self.parse_eq()
        while self.peek() == '&':
            self.take()
            v = v & self.parse_eq()
        return v

    def parse_eq(self):
        v = self.parse_rel()
        while self.peek() in ('==', '!='):
            op = self.take()
            r = self.parse_rel()
            v = int(v == r) if op == '==' else int(v != r)
        return v

    def parse_rel(self):
        v = self.parse_shift()
        while self.peek() in ('<', '>', '<=', '>='):
            op = self.take()
            r = self.parse_shift()
            if op == '<':
                v = int(v < r)
            elif op == '>':
                v = int(v > r)
            elif op == '<=':
                v = int(v <= r)
            else:
                v = int(v >= r)
        return v

    def parse_mul(self):
        v = self.parse_unary()
        while self.peek() in ('*', '/', '%'):
            op = self.take()
            r = self.parse_unary()
            if op == '*':
                v = v * r
            elif r == 0:
                if not self.skip:
                    raise OvMergeError("* Division by zero in #if expression")
                v = 0
            elif op == '/':
                v = _c_div(v, r)
            else:
                v = _c_mod(v, r)
        return v

    def parse_unary(self):
        if self.peek() == '!':
            self.take()
            return int(not self.parse_unary())
        return super().parse_unary()

    def parse_primary(self):
        t = self.peek()
        if t == 'defined':
            self.take()
            paren = self.peek() == '('
            if paren:
                self.take()
            name = self.take()
            if not _IDENT_RE.match(name):
                raise OvMergeError("* 'defined' requires a macro name")
            if paren and self.take() != ')':
                raise OvMergeError("* Missing ')' after 'defined'")
            return int(name in self.defines)
        if t is not None and _IDENT_RE.match(t):
            self.take()
            if self.peek() == '(':
                raise OvMergeError(f"* Function-like macro '{t}' not supported in #if expression")
            val = self.defines.get(t)
            if val is None or t in self.expanding:
                # Undefined (or recursive) identifiers evaluate to 0, as in cpp
                return 0
            if val.strip() == '':
                raise OvMergeError(f"* '{t}' is defined with no value")
            return eval_pp_expr(val, self.defines, self.expanding | {t})
        return super().parse_primary()


def eval_pp_expr(s, defines, expanding=frozenset()):
    s = re.sub(r'/\*.*?\*/', ' ', s)
    s = re.sub(r'//.*', '', s)
    tokens = _tokenize_pp_expr(s)
    return _PPExprParser(tokens, defines, expanding).parse()


def integer_value(value, size):
    if value is None:
        return None
    if re.fullmatch(r'(y|yes|on|true|down)?', value):
        return 1
    if re.fullmatch(r'n|no|off|false|none', value):
        return 0
    if re.fullmatch(r'up', value):
        return 2
    if value.startswith('&'):
        if size != 4:
            raise OvMergeError(f"* Label '{value}' used as non-32-bit integer")
        return value
    if re.match(r'^[0-9]', value):
        mask = {1: 0xff, 2: 0xffff, 4: 0xffffffff, 8: 0xffffffffffffffff}.get(size)
        if mask is None:
            raise OvMergeError(f"* Bad size '{size}' for integer")
        return eval_expr(value) & mask
    if re.fullmatch(r'[A-Z][A-Z0-9_]+', value):
        return value
    if re.fullmatch(r'\(.+\)', value):
        return value
    raise OvMergeError(f"* Bad integer value '{value}'")


def boolean_value(value, strict):
    if re.fullmatch(r'(y|yes|on|true|okay)?', value):
        return True
    if re.fullmatch(r'n|no|off|false|disabled', value):
        return False
    if not re.match(r'^[0-9]', value):
        if strict:
            raise OvMergeError(f"* Bad boolean value '{value}'")
        return value != ""
    m = re.match(r'[0-9]+', value)
    return int(m.group(0)) != 0


# ---------------------------------------------------------------------------
# Node / label / property helpers
# ---------------------------------------------------------------------------

def add_node(parent, name):
    node = name if isinstance(name, list) else new_node(name)
    if S.trace_tree:
        print("[ add_node %s -> %s ]" % (node[NAME] if node[NAME] is not None else "?",
                                          parent[NAME] if parent else "-"))
    node[PARENT] = parent
    if parent is not None:
        node[DEPTH] = parent[DEPTH] + 1
        parent[CHILDREN].append(node)
    else:
        if name != '/':
            raise OvMergeError(f"* Invalid root node '{name}'")
        node[DEPTH] = 0
        S.cur_dt.root = node
    return node


def get_node(dt, path):
    node = dt.root
    m = re.match(r'^([^/]+)(/|$)', path)
    if m:
        path = '/' + path[m.end():]
        node = resolve_alias(dt, m.group(1))
    if path == '/':
        return node
    pos = 0
    seg_re = re.compile(r'/([-a-zA-Z0-9,._+#@]+)')
    while node is not None:
        m2 = seg_re.match(path, pos)
        if not m2:
            break
        node = get_child(node, m2.group(1))
        pos = m2.end()
    return node


def is_node_empty(node):
    return not get_children(node) and not get_props(node)


def get_child(node, name):
    if node is not None:
        for child in node[CHILDREN]:
            if child[NAME] == name or ('@' not in name and re.match(name + '@', child[NAME])):
                return child
        return None
    else:
        if name == '/':
            return S.cur_dt.root if S.cur_dt else None
        return None


def _lenient_hex(s):
    # Mirrors Perl's hex(), which parses a leading run of hex digits and
    # warns (but doesn't fail) about anything else, defaulting to 0.
    m = re.match(r'[0-9a-fA-F]*', s)
    digits = m.group(0) if m else ''
    return int(digits, 16) if digits else 0


def _by_addr_key(node):
    m = re.search(r'@(.*)$', node[NAME])
    return _lenient_hex(m.group(1)) if m else None


import functools


def _by_addr_cmp(a, b):
    a_addr = _by_addr_key(a)
    b_addr = _by_addr_key(b)
    if a_addr is not None and b_addr is not None:
        return (a_addr > b_addr) - (a_addr < b_addr)
    if a_addr is not None:
        return -1
    if b_addr is not None:
        return 1
    return (a[NAME] > b[NAME]) - (a[NAME] < b[NAME])


def get_children(node):
    if S.sort:
        return sorted(node[CHILDREN], key=functools.cmp_to_key(_by_addr_cmp))
    return list(node[CHILDREN])


def get_labels(node):
    if S.sort:
        return sorted(node[LABELS])
    return list(node[LABELS])


def node_path(node):
    if node[NAME] == '/':
        return '/'
    parent_path = node_path(node[PARENT])
    if parent_path == '/':
        parent_path = ''
    return parent_path + '/' + node[NAME]


def add_label(dt, node, label, move_from=None):
    if S.trace_tree:
        print("[ add_label %s -> %s ]" % (label, node[NAME]))
    old_value = dt.labels.get(label)
    if old_value is not None:
        if old_value is node:
            return
        if move_from is not None and old_value is not move_from:
            raise OvMergeError(f"* Label '{label}' redefined")
        if move_from is not None:
            move_from[LABELS] = [l for l in move_from[LABELS] if l != label]
    dt.labels[label] = node
    node[LABELS].append(label)
    if S.warnings and len(node[LABELS]) > 1:
        print("* Multiple labels on '" + node_path(node) + "'")


def resolve_label(dt, label):
    return dt.labels.get(label)


def resolve_alias(dt, alias):
    aliases = get_node(dt, '/aliases')
    prop = get_prop(aliases, alias)
    if prop is None:
        return None
    if prop[1][0] == '&':
        return resolve_label(dt, prop[1][1])
    else:
        return get_node(dt, prop[1][1])


def fragment_of(node):
    if node is None:
        return None
    if re.match(r'^fragment@', node[NAME]):
        return node
    return fragment_of(node[PARENT])


# ---------------------------------------------------------------------------
# Property helpers
# ---------------------------------------------------------------------------

def get_prop_string(node, name):
    prop = get_prop(node, name)
    if prop is None:
        return None
    if len(prop) != 2:
        return None
    if prop[1][0] != '"':
        return None
    return prop[1][1]


def get_prop(node, name):
    if node is None:
        return None
    for prop in node[PROPS]:
        if prop[0] == name:
            return prop
    return None


def get_props(node):
    if S.sort:
        return sorted(node[PROPS], key=lambda p: p[0])
    return list(node[PROPS])


def add_prop(node, name, *vals):
    new = [name] + list(vals)
    node[PROPS].append(new)
    return new


def set_prop(node, name, *vals):
    if name == S.trace_prop:
        pass
    adj_val_refs(1, vals)
    for prop in node[PROPS]:
        if prop[0] == name:
            adj_val_refs(-1, prop[1:])
            prop[1:] = list(vals)
            return prop
    return add_prop(node, name, *vals)


def apply_prop(node, name, *vals):
    vals = list(vals)
    if name == 'status':
        vals = [['"', 'okay' if boolean_value(vals[0][1], True) else 'disabled']]
    elif name == 'bootargs':
        vals = [['"', get_prop(node, name)[1][1] + ' ' + vals[0][1]]]
    return set_prop(node, name, *vals)


def delete_prop(node, name):
    if name == S.trace_prop:
        pass
    for i, prop in enumerate(node[PROPS]):
        if prop[0] == name:
            adj_val_refs(-1, prop[1:])
            return node[PROPS].pop(i)
    return None


def find_prop_chunk(node, propname, offset, size, ovrname, create):
    prop = get_prop(node, propname)
    if prop is None and create:
        prop = set_prop(node, propname, ['<', size, []])
    if prop is None:
        return (None, 0)

    chunk = None
    pos = 0
    for i in range(1, len(prop)):
        chunk = prop[i]
        typ = chunk[0]
        if typ == '"':
            end = pos + len(chunk[1]) + 1
        elif typ == '[':
            end = pos + len(chunk[2])
        else:
            end = pos + chunk[1] * len(chunk[2])
        if offset < end:
            break
        pos = end

    if chunk is None and create:
        chunk = ['<', size, []]
        prop.append(chunk)

    offset -= pos
    if size and offset % size:
        raise OvMergeError(f"* Unaligned override '{ovrname}', property {propname}")
    return (chunk, offset // size if size else 0)


def adj_val_refs(inc, vals):
    for val in vals:
        if val[0] == '&':
            adj_ref(inc, val[1])
        elif val[0] == '<':
            for elem in val[2]:
                if isinstance(elem, str) and elem.startswith('&'):
                    adj_ref(inc, elem[1:])


def adj_ref(inc, label):
    if S.cur_dt is None:
        return
    S.cur_dt.refcount[label] = S.cur_dt.refcount.get(label, 0) + inc
    if label == S.trace_label:
        print("[ ref %s -> %s ]" % (label, S.cur_dt.refcount[label]))


# ---------------------------------------------------------------------------
# Vectors / overrides
# ---------------------------------------------------------------------------

def get_vector(p, size, length=None):
    if p is None:
        return None
    if (p[0] == '<' or p[0] == '[') and (p[1] == size or (p[1] == 4 and size == 8)) and \
            (length is None or length == len(p[2])):
        return p[2]
    return None


def get_label_ref(p):
    vector = get_vector(p, 4, 1)
    if vector is not None and re.fullmatch(r'&.*|0', vector[0]):
        return vector[0]
    return None


def _aget(lst, idx):
    return lst[idx] if 0 <= idx < len(lst) else None


def parse_lookup_table(table, ovr, ppos, value):
    val = None
    have_val = False
    pos = 0
    pattern = re.compile(r"(?:'([^']*)'|([^=,}]*))(?:=(?:'([^']*)'|([^,}]*)))?([,}])?")

    while True:
        m = pattern.match(table, pos)
        if not m:
            break
        if m.end() == pos and m.group(0) == '':
            break
        pos = m.end()
        g1, g2, g3, g4, sep = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
        key = g1 if g1 is not None else g2
        sub = g3 if g3 is not None else g4
        if sep is None:
            p = _aget(ovr, ppos[0])
            ppos[0] += 1
            vec = get_vector(p, 4, 1)
            sub = vec[0] if vec is not None else None
            nxt = _aget(ovr, ppos[0])
            ppos[0] += 1
            if nxt is None:
                sep = '}'
            else:
                if nxt[0] != '"':
                    raise OvMergeError("* Expected string in lookup table")
                table = nxt[1]
                pos = 0
                mm = re.match(r'(\})', table)
                if mm:
                    sep = mm.group(1)
        if value is not None:
            if key == '':
                if not have_val:
                    val = sub if sub is not None else value
                    have_val = True
            elif key == value:
                val = sub if sub is not None else key
                have_val = True
        if (sep or '') == '}':
            break

    if value is not None and val is None:
        raise OvMergeError(f"* No match for '{value}'")

    return val


def parse_fragment_ops(decl):
    ops = []
    p = 0
    while True:
        m = re.match(r'([=!+-])(\d+)', decl[p:])
        if not m:
            break
        ops.append((m.group(1), m.group(2)))
        p += m.end()
    return ops


def dtparam(dt, param, value, wakeable=None, pinctrl_refs=None, label_use=None):
    overrides = get_node(dt, '/__overrides__')
    if overrides is None:
        raise OvMergeError("* No overrides found")
    ovr = get_prop(overrides, param)
    if ovr is None:
        raise OvMergeError(f"* dtparam '{param}' not found")

    pos = 1
    n = len(ovr)
    while pos < n:
        p = ovr[pos]
        pos += 1
        label = get_label_ref(p)
        if label is None:
            raise OvMergeError(f"* Invalid override 1: {param}")
        p = _aget(ovr, pos)
        pos += 1
        if p is None or p[0] != '"':
            raise OvMergeError(f"* Invalid override 2: {param}")
        decl = p[1]

        m_label = re.match(r'^&(.*)', label)
        if m_label:
            node = resolve_label(dt, m_label.group(1))
            if node is None:
                raise OvMergeError(f"* Override '{param}' targets unknown label '{m_label.group(1)}'")

            if label_use is not None:
                pm = re.match(r'^([-a-zA-Z0-9_,]+)', decl)
                if pm:
                    label_use(m_label.group(1), pm.group(1))

            m = re.match(r'^([-a-zA-Z0-9_,]+)([.;:#])(\d+)(?:(=|\{)(.*))?$', decl)
            if m:
                prop, typ, offset, op, opdata = m.group(1), m.group(2), int(m.group(3)), \
                    (m.group(4) or ''), m.group(5)
                size = ELEM_SIZES[typ]
                val = value
                if op == '=':
                    if opdata:
                        val = opdata
                    else:
                        vector = get_vector(_aget(ovr, pos), 4, 1)
                        pos += 1
                        if vector is None:
                            raise OvMergeError(f"* Expected cell value in parameter '{param}'")
                        val = vector[0]
                elif op == '{':
                    ppos = [pos]
                    val = parse_lookup_table(opdata, ovr, ppos, value)
                    pos = ppos[0]

                intval = integer_value(val, size)
                if (pinctrl_refs is not None and prop == 'pinctrl-0' and
                        isinstance(intval, str) and intval.startswith('&')):
                    pinctrl_refs.add(intval[1:])
                if value is not None and prop == 'reg':
                    regval = format(intval & 0xffffffff, 'x') if isinstance(intval, int) else '0'
                    node[NAME] = re.sub(r'@[0-9a-fA-F]*$', '@' + regval, node[NAME])

                chunk, chunk_idx = find_prop_chunk(
                    node, prop, offset, size, param, (value is not None) and prop != 'reg')

                if chunk is not None:
                    vector = get_vector(chunk, size)
                    if vector is None:
                        raise OvMergeError(f"* Probably incorrect override property type for '{prop}'")

                    if value is not None:
                        for i in range(len(vector), chunk_idx):
                            vector.append(0)
                        if chunk_idx < len(vector):
                            vector[chunk_idx] = intval
                        else:
                            vector.append(intval)

            elif re.match(r'^([-a-zA-Z0-9_,]+)([?!])(?:(=|\{)(.*))?$', decl):
                mm = re.match(r'^([-a-zA-Z0-9_,]+)([?!])(?:(=|\{)(.*))?$', decl)
                prop, sense, op, opdata = mm.group(1), mm.group(2), (mm.group(3) or ''), mm.group(4)
                val = value
                if op == '=':
                    if opdata:
                        val = opdata
                    else:
                        vector = get_vector(_aget(ovr, pos), 4, 1)
                        pos += 1
                        if vector is None:
                            raise OvMergeError(f"* Expected cell value in parameter '{param}'")
                        val = vector[0]
                elif op == '{':
                    ppos = [pos]
                    val = parse_lookup_table(opdata, ovr, ppos, value)
                    pos = ppos[0]

                if value is not None:
                    boolval = boolean_value(val, True)
                    if sense == '!':
                        boolval = not boolval
                    if boolval:
                        set_prop(node, prop)
                    else:
                        delete_prop(node, prop)

            elif re.match(r'^([-a-zA-Z0-9_,]+)\[(?:(=|\{)(.*))?$', decl):
                mm = re.match(r'^([-a-zA-Z0-9_,]+)\[(?:(=|\{)(.*))?$', decl)
                prop, op, opdata = mm.group(1), (mm.group(2) or ''), mm.group(3)
                val = value
                if op == '=':
                    val = opdata
                elif op == '{':
                    ppos = [pos]
                    val = parse_lookup_table(opdata, ovr, ppos, value)
                    pos = ppos[0]
                if value is not None:
                    apply_prop(node, prop, ['[', 1, byte_array_value(val)])

            elif re.match(r'^([-a-zA-Z0-9_,]+)(?:(=|\{)(.*))?$', decl):
                mm = re.match(r'^([-a-zA-Z0-9_,]+)(?:(=|\{)(.*))?$', decl)
                prop, op, opdata = mm.group(1), (mm.group(2) or ''), mm.group(3)
                val = value
                if op == '=':
                    if opdata:
                        val = opdata
                    elif pos == n:
                        val = ''
                    else:
                        val = _aget(ovr, pos)
                        pos += 1
                        if val is None or (val[0] != '"' and val[0] != '&'):
                            raise OvMergeError(
                                f"* Expected a string or label reference in parameter '{param}'")
                elif op == '{':
                    ppos = [pos]
                    val = parse_lookup_table(opdata, ovr, ppos, value)
                    pos = ppos[0]
                if value is not None:
                    if prop == 'name':
                        node[NAME] = val
                    else:
                        apply_prop(node, prop, val if isinstance(val, list) else ['"', val])

            else:
                raise OvMergeError(f"* Invalid parameter declaration '{decl}'")
        else:
            for op, num in parse_fragment_ops(decl):
                frag = get_node(dt, '/fragment-' + num) or get_node(dt, '/fragment@' + num)
                if frag is None:
                    raise OvMergeError(f"* Param {param}: no fragment {num}")
                if wakeable is not None and op != '-':
                    wakeable.add(num)
                if value is not None:
                    boolval = boolean_value(value, False)
                    if op == '!':
                        boolval = not boolval
                    elif op == '+':
                        boolval = True
                    elif op == '-':
                        boolval = False
                    frag[CHILDREN][0][NAME] = '__overlay__' if boolval else '__dormant__'


def find_wakeable_fragments(dt):
    """Fragment numbers ('0', '1', ...) reachable from a __dormant__ state via
    some __overrides__ declaration (any op other than '-' on that fragment)."""
    wakeable = set()
    overrides = get_node(dt, '/__overrides__')
    for prop in (get_props(overrides) if overrides is not None else []):
        dtparam(dt, prop[0], None, wakeable=wakeable)
    return wakeable


# ---------------------------------------------------------------------------
# Tree mutation
# ---------------------------------------------------------------------------

def remove_node(node):
    parent = node[PARENT]
    if S.trace_tree:
        print("[ remove_node %s ]" % node[NAME])
    if parent is None:
        return
    node[PARENT] = None
    found = None
    for i, child in enumerate(parent[CHILDREN]):
        if child is node:
            found = i
            break
    if found is None:
        raise OvMergeError("* Internal error - wrong parent/missing child")
    del parent[CHILDREN][found]


def delete_node(node):
    if node is None:
        return None

    if S.trace_tree:
        print("[ delete node %s ]" % node[NAME])
    remove_node(node)

    for label in get_labels(node):
        S.cur_dt.labels.pop(label, None)

    for prop in get_props(node):
        adj_val_refs(-1, prop[1:])

    while node[CHILDREN]:
        delete_node(node[CHILDREN][0])

    return True


def relabel_node(node, transform, depth):
    for prop in get_props(node):
        if depth > 0:
            for chunk in prop[1:]:
                if chunk[0] == '<':
                    vals = chunk[2]
                    for idx in range(len(vals)):
                        term = vals[idx]
                        if isinstance(term, str):
                            m = re.match(r'^&(.*)', term)
                            if m:
                                newlabel = transform.get(m.group(1))
                                if newlabel:
                                    adj_ref(-1, m.group(1))
                                    adj_ref(1, newlabel)
                                    vals[idx] = '&' + newlabel
                elif chunk[0] == '&':
                    newlabel = transform.get(chunk[1])
                    if newlabel:
                        adj_ref(-1, chunk[1])
                        adj_ref(1, newlabel)
                        chunk[1] = newlabel

    for subnode in get_children(node):
        relabel_node(subnode, transform, depth + 1)


def apply_node(base, dst, src):
    for prop in get_props(src):
        apply_prop(dst, prop[0], *prop[1:])

    for label in get_labels(src):
        add_label(base, dst, label, src)

    for subsrc in get_children(src):
        subdst = get_child(dst, subsrc[NAME])
        if subdst and not S.force:
            raise OvMergeError(f"* Subnode {subsrc[NAME]} already exists")
        else:
            subdst = add_node(dst, subsrc[NAME])
        apply_node(base, subdst, subsrc)


# ---------------------------------------------------------------------------
# Overlay merging
# ---------------------------------------------------------------------------

def get_fragments(ov):
    fragments = []
    for child in get_children(ov.root):
        if re.match(r'^fragment[@-](\d+)$', child[NAME]):
            fragments.append(child)
    return fragments


def renumber_fragments(ov, offset):
    fragments = []
    remap = {}
    count = 0
    overrides = None

    for child in get_children(ov.root):
        m = re.fullmatch(r'fragment([@-])(\d+)', child[NAME])
        if m:
            sep, num = m.group(1), int(m.group(2))
            remap[num] = count + offset
            child[NAME] = 'fragment%s%d' % (sep, count + offset)
            fragments.append(child)
            count += 1
        elif child[NAME] == '__overrides__':
            overrides = child

    ov.frag_count = count

    if overrides is None:
        return

    for ovr in overrides[PROPS]:
        pos = 1
        n = len(ovr)
        while pos + 1 < n:
            if (get_label_ref(ovr[pos]) or '') == '0':
                pos += 1
                chunk = ovr[pos]
                decl = chunk[1]

                p = 0
                while True:
                    m2 = re.match(r'[=!+-](\d+)', decl[p:])
                    if not m2:
                        break
                    fragnum = int(m2.group(1))
                    if fragnum not in remap:
                        raise OvMergeError(
                            "* override '" + str(ovr[0]) +
                            "}' references missing fragment " + str(fragnum))
                    p += m2.end()

                out = []
                p = 0
                while True:
                    m2 = re.match(r'([=!+-])(\d+)', decl[p:])
                    if not m2:
                        break
                    out.append(m2.group(1) + str(remap[int(m2.group(2))]))
                    p += m2.end()
                out.append(decl[p:])
                chunk[1] = ''.join(out)
            pos += 1


def ovmerge(base, ov):
    if not base.plugin or not ov.plugin:
        raise OvMergeError("* Cannot merge a non-overlay")

    for inc in list(ov.includes):
        set_add(base.includes, inc)

    renumber_fragments(ov, base.frag_count)

    transform = {}
    base_labels = base.labels
    ov_labels = ov.labels

    for l in list(ov_labels.keys()):
        nl = l
        n = ov_labels[l]
        if base_labels.get(l):
            i = 1
            while True:
                nl = f"{l}_{i}"
                if not base_labels.get(nl):
                    break
                i += 1
            transform[l] = nl
            for idx, ol in enumerate(n[LABELS]):
                if ol == l:
                    n[LABELS][idx] = nl
        base_labels[nl] = n

    relabel_node(ov.root, transform, 0)

    base_overrides = get_node(base, '/__overrides__')
    ov_overrides = get_node(ov, '/__overrides__')

    if base_overrides:
        remove_node(base_overrides)

    for child in get_fragments(ov):
        add_node(base.root, child)
        base.frag_count += 1

    if ov_overrides:
        if not base_overrides:
            base_overrides = new_node('__overrides__')
        for ovr in ov_overrides[PROPS]:
            if get_prop(base_overrides, ovr[0]):
                raise OvMergeError(f"* Duplicate parameter '{ovr[0]}'")
            set_prop(base_overrides, ovr[0], *ovr[1:])

    if base_overrides:
        add_node(base.root, base_overrides)


def ovstrip(dt):
    if S.trace_tree:
        print("[ ovstrip ]")
    S.cur_dt = dt

    unused = [key for key, value in dt.labels.items() if not dt.refcount.get(key)]

    for label in unused:
        node = dt.labels[label]
        del dt.labels[label]
        for i, l in enumerate(node[LABELS]):
            if l == label:
                del node[LABELS][i]
                break

    S.cur_dt = None


def ovapply1(ov):
    if not ov.plugin:
        raise OvMergeError("* Cannot apply a non-overlay")

    for fragment in get_fragments(ov):
        overlay = get_child(fragment, '__overlay__')
        if overlay is None:
            continue
        if S.trace_tree:
            print("[ apply fragment %s ]" % fragment[NAME])
        target_node = None
        target = get_prop(fragment, 'target')
        if target:
            label = get_label_ref(target[1])
            m = re.match(r'^&(.*)', label) if label is not None else None
            if not m:
                raise OvMergeError("* Invalid target reference")
            target_node = ov.labels.get(m.group(1))
            if target_node:
                apply_node(ov, target_node, overlay)
                overlay[NAME] = '__dormant__'


def ovapply2(base, ov):
    if not ov.plugin:
        raise OvMergeError("* Cannot apply a non-overlay")
    if base.plugin:
        raise OvMergeError("* Cannot apply an overlay to an overlay")

    for inc in list(ov.includes):
        set_add(base.includes, inc)

    for fragment in get_fragments(ov):
        overlay = get_child(fragment, '__overlay__')
        if overlay is None:
            continue
        if S.trace_tree:
            print("[ apply fragment %s to base tree]" % fragment[NAME])
        target_node = None
        target = get_prop(fragment, 'target')
        if target:
            label = get_label_ref(target[1])
            m = re.match(r'^&(.*)', label) if label is not None else None
            if not m:
                raise OvMergeError("* Invalid target reference")
            target_node = base.labels.get(m.group(1))
            if target_node is None:
                raise OvMergeError(f"* Label '{m.group(1)}' not found in base")
        else:
            target = get_prop(fragment, 'target-path')
            if target[1][0] != '"':
                raise OvMergeError("* Invalid target-path")
            target_node = get_node(base, target[1][1])
            if target_node is None:
                raise OvMergeError(f"* Path '{target[1][1]}' not found in base")

        apply_node(base, target_node, overlay)


def ordercheck(ov, applied):
    for fragment in get_fragments(ov):
        overlay = get_child(fragment, '__overlay__') or get_child(fragment, '__dormant__')
        if overlay is None:
            continue
        target = get_prop(fragment, 'target')
        if target:
            label = get_label_ref(target[1])
            m = re.match(r'^&(.*)', label) if label is not None else None
            if not m:
                raise OvMergeError("* Invalid target reference")
            target_node = ov.labels.get(m.group(1))
            if target_node:
                target_fragment = fragment_of(target_node)
                if applied.get(id(target_node)):
                    raise OvMergeError(
                        "* %s should precede %s" %
                        (fragment[NAME], target_fragment[NAME] if target_fragment else "fragment@?"))
                set_applied(overlay, applied)


def set_applied(node, applied):
    applied[id(node)] = True
    for subnode in get_children(node):
        set_applied(subnode, applied)


# ---------------------------------------------------------------------------
# Sets
# ---------------------------------------------------------------------------

def set_add(lst, val):
    if isinstance(val, list):
        for existing in lst:
            if existing is val:
                return
        lst.append(val)
    else:
        if val not in lst:
            lst.append(val)


# ---------------------------------------------------------------------------
# Dump
# ---------------------------------------------------------------------------

def dtdump(dt, out):
    out.write("/dts-v1/;\n")
    if dt.plugin:
        out.write("/plugin/;\n")
    out.write("\n")

    if dt.includes:
        for inc in dt.includes:
            out.write(f"#include {inc}\n")
        out.write("\n")

    if dt.defines:
        for name, value in dt.defines.items():
            out.write(f"#define {name} {value}\n")
        out.write("\n")

    if dt.memreserves:
        for res in dt.memreserves:
            out.write(f"/memreserve/ {res[0]} {res[1]};\n")
        out.write("\n")

    dump_node(dt.root, 0, out)


def dump_node(node, depth, out):
    indent = S.indent_str * depth

    out.write(indent + ': '.join(get_labels(node) + [node[NAME]]) + " {\n")

    for prop in get_props(node):
        terms = []
        out.write(indent + S.indent_str + prop[0])
        for chunk in prop[1:]:
            if chunk[0] == '"':
                terms.append('"' + chunk[1] + '"')
            elif chunk[0] == '&':
                terms.append('&' + chunk[1])
            elif chunk[0] == '<':
                prefix = ("/bits/ %d " % (chunk[1] * 8)) if chunk[1] != 4 else ''
                terms.append(prefix + '<' + ' '.join(str(v) for v in chunk[2]) + '>')
            elif chunk[0] == '[':
                terms.append('[' + byte_array_string(chunk[2]) + ']')
            else:
                terms.append('?')
        if terms:
            out.write(' = ' + ', '.join(terms))
        out.write(";\n")

    for subnode in get_children(node):
        dump_node(subnode, depth + 1, out)

    out.write(indent + "};\n")


# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

_IF_RE = re.compile(r'^\s*#if(def|ndef)?(?!\w)\s*(.*?)\s*$')
_ELIF_RE = re.compile(r'^\s*#elif(?!\w)\s*(.*?)\s*$')
_ELSE_RE = re.compile(r'^\s*#else')
_ENDIF_RE = re.compile(r'^\s*#endif')
_INCLUDE_RE = re.compile(r'^\s*(?:#include|/include/)\s+(["<][^">]+[">])\s*$')
_DEFINE_RE = re.compile(r'^\s*#define\s+(\w+)(?:\s+([^\r\n]+))?')
_UNDEF_RE = re.compile(r'^\s*#undef\s+(\w+)')
_BKPT_RE = re.compile(r'^\s*#bkpt')
_WS_RE = re.compile(r'\s*')
_WORD_RE = re.compile(r'\b(\w+)\b')
_PAREN_RE = re.compile(r'(.*?)([()])(\s*)')
_TAIL_RE = re.compile(r'(.*?)\s*$')
_COMMENT_END_RE = re.compile(r'.*?\*/')
_BLOCK_COMMENT_RE = re.compile(r'.*?\*/\s*')

_TOKEN_RE = re.compile(
    r'((?:/(?:dts-v1|plugin|memreserve|bits|delete-node|delete-property)/)'
    r'|&[a-zA-Z_][a-zA-Z0-9_]*'
    r'|[a-zA-Z_][a-zA-Z0-9_]*:'
    r'|[-a-zA-Z0-9,._+#@]+'
    r'|\('
    r'|"(?:[^\\"]|\\.)*"'
    r"|'(?:[^']|\\.)*'"
    r'|//'
    r'|/\*'
    r'|[/{};=<>,\[\]])')


def search_path(fname):
    if S.branch:
        r = subprocess.run(['git', 'cat-file', '-e', f'{S.branch}:./{fname}'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            return fname
    if os.access(fname, os.R_OK):
        return fname
    return None


def open_source_lines(filename):
    if S.branch:
        proc = subprocess.run(['git', 'show', f'{S.branch}:./{filename}'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            raise OvMergeError(f"* Failed to open '{filename}'")
        return proc.stdout.decode('utf-8', errors='replace').splitlines(keepends=True)
    else:
        try:
            with open(filename, 'r') as f:
                return f.readlines()
        except OSError:
            raise OvMergeError(f"* Failed to open '{filename}'")


def eval_if_expr(expr, defines, filename, linenum):
    try:
        return eval_pp_expr(expr, defines)
    except OvMergeError as e:
        raise OvMergeError(f"{e} ({filename}:{linenum})")


def read_tokens(filename, depth, defines):
    linenum = 0
    tokens = [FileMarker(filename)]
    in_comment = False
    # Each entry is [parent_active, branch_taken, active]
    cond_stack = []
    expr = None
    expr_level = 0
    filepath = re.sub(r'/?[^/]*$', '', filename)
    if filepath:
        filepath += '/'

    if S.show_includes:
        print("    " * depth + filename)
    if S.trace_parse:
        print(f"[read_tokens '{filename}']")
    if S.expand and not S.expand_label:
        print(f"#### Start of '{filename}'")

    lines = open_source_lines(filename)

    for line in lines:
        linenum += 1

        if in_comment:
            m = _COMMENT_END_RE.match(line)
            if not m:
                continue
            line = line[m.end():]
            in_comment = False

        m = _IF_RE.match(line)
        if m:
            mode = m.group(1)
            parent_active = not cond_stack or cond_stack[-1][2]
            val = False
            if parent_active:
                if mode:
                    m2 = re.match(r'^(\w+)', m.group(2))
                    if not m2:
                        raise OvMergeError(f"* #if{mode} requires a macro name ({filename}:{linenum})")
                    val = (m2.group(1) in defines) == (mode == 'def')
                else:
                    val = eval_if_expr(m.group(2), defines, filename, linenum) != 0
            cond_stack.append([parent_active, val, parent_active and val])
            continue

        m = _ELIF_RE.match(line)
        if m:
            if not cond_stack:
                raise OvMergeError(f"* #elif without #if ({filename}:{linenum})")
            state = cond_stack[-1]
            if state[0] and not state[1]:
                val = eval_if_expr(m.group(1), defines, filename, linenum) != 0
                state[1] = state[2] = val
            else:
                state[2] = False
            continue

        if _ELSE_RE.match(line):
            if not cond_stack:
                raise OvMergeError(f"* #else without #if ({filename}:{linenum})")
            state = cond_stack[-1]
            state[2] = state[0] and not state[1]
            state[1] = True
            continue

        if _ENDIF_RE.match(line):
            if not cond_stack:
                raise OvMergeError(f"* Unmatched #endif ({filename}:{linenum})")
            cond_stack.pop()
            continue

        if cond_stack and not cond_stack[-1][2]:
            continue

        m = _INCLUDE_RE.match(line)
        if m:
            incfile = m.group(1)
            if re.search(r'\.h.$', incfile):
                tokens.append('#include')
                tokens.append(incfile)
            elif re.search(r'\.dtsi?.$', incfile):
                dtsfile = search_path(filepath + incfile[1:-1])
                if not dtsfile:
                    raise OvMergeError(f"* Failed to find include file '{incfile}'")
                inc_tokens = read_tokens(dtsfile, depth + 1, defines)
                tokens.extend(inc_tokens)
                tokens.append(FileMarker(filename))
                if S.expand and not S.expand_label:
                    print(f"#### Continue '{filename}'")
            else:
                raise OvMergeError(f"* Invalid include file '{incfile}'")
            continue

        m = _DEFINE_RE.match(line)
        if m:
            symbol = m.group(1)
            val = m.group(2) if m.group(2) is not None else ''
            val = re.sub(r'//.*', '', val)
            val = re.sub(r'\s+$', '', val)
            defines[symbol] = val
            continue

        m = _UNDEF_RE.match(line)
        if m:
            defines.pop(m.group(1), None)
            continue

        if _BKPT_RE.match(line):
            continue

        if S.expand_label:
            sys.stdout.write(f"{filename}:{linenum}: ")
        if S.expand:
            sys.stdout.write(line)

        pos = _WS_RE.match(line).end()

        if expr_level:
            while expr_level:
                mm = _PAREN_RE.match(line, pos)
                if not mm:
                    break
                pos = mm.end()
                expr_level += 1 if mm.group(2) == '(' else -1
                expr += mm.group(1) + mm.group(2)
                if expr_level:
                    expr += mm.group(3)
            if expr_level:
                mm = _TAIL_RE.match(line, pos)
                if mm:
                    expr += mm.group(1)
                    pos = mm.end()
                continue
            tokens.append(expr)

        while True:
            m = _TOKEN_RE.match(line, pos)
            if not m:
                break
            tok = m.group(1)
            pos = m.end()
            wsm = _WS_RE.match(line, pos)
            pos = wsm.end()

            if tok == '//':
                pos = len(line)
                break
            elif tok == '/*':
                mm = _BLOCK_COMMENT_RE.match(line, pos)
                if mm:
                    pos = mm.end()
                    continue
                else:
                    in_comment = True
                    pos = len(line)
                    break
            elif tok == '(':
                expr_level = 1
                expr = '('
                while expr_level:
                    mm = _PAREN_RE.match(line, pos)
                    if not mm:
                        break
                    pos = mm.end()
                    expr_level += 1 if mm.group(2) == '(' else -1
                    expr += mm.group(1) + mm.group(2)
                    if expr_level:
                        expr += mm.group(3)
                if expr_level:
                    mm = _TAIL_RE.match(line, pos)
                    if mm:
                        expr += mm.group(1)
                        pos = mm.end()
                    break
                tok = expr

            wm = _WORD_RE.search(tok)
            if wm:
                sym = wm.group(1)
                newsym = defines.get(sym)
                if newsym is not None:
                    if S.trace_parse:
                        print(f"['{sym}' -> '{newsym}']")
                    tok = re.sub(r'\b' + re.escape(sym) + r'\b', lambda _m: newsym, tok, count=1)
            tokens.append(tok)

        rest = line[pos:]
        if not re.fullmatch(r'[\r\n]*', rest):
            raise OvMergeError(f"* Bad token at '{rest}'")

    if cond_stack:
        raise OvMergeError(f"* Unterminated #if at end of '{filename}'")

    if S.expand and not S.expand_label:
        print(f"#### End of '{filename}'")

    return tokens


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def get_head(ps):
    while True:
        if ps.pos >= len(ps.tokens):
            return None
        head = ps.tokens[ps.pos]
        if isinstance(head, FileMarker):
            ps.file = head.filename
            if S.trace_parse:
                print(f"[file {head.filename}]")
            ps.pos += 1
            continue
        return head


def get_next(ps):
    ps.pos += 1
    return get_head(ps)


def match(ps, tok):
    head = get_head(ps)
    if S.trace_parse:
        print(f"[match '{tok}' @ {ps.pos}]")
    if head != tok:
        raise OvMergeError(f"* Unexpected token '{head}' - expected '{tok}'")
    return get_next(ps)


def get_int(ps):
    head = get_head(ps)
    if head is None or not re.match(r'^[0-9]', head):
        return None
    get_next(ps)
    return head


def parse_node(ps, parent, depth, node, *newlabels):
    next_tok = match(ps, '{')

    if not isinstance(node, list):
        child = get_child(parent, node)
        node = child if child is not None else add_node(parent, node)

    if S.trace_parse:
        print(f"parse_node({node[NAME]}, {depth} ...)")

    while next_tok != '}':
        childlabels = []

        if next_tok == '/delete-node/':
            next_tok = match(ps, next_tok)
            if next_tok is not None and re.fullmatch(r'[-a-zA-Z0-9,._+#@]+', next_tok):
                delete_node(get_child(node, next_tok))
                match(ps, next_tok)
                next_tok = match(ps, ';')
            continue
        elif next_tok == '/delete-property/':
            next_tok = match(ps, next_tok)
            if next_tok is not None and re.fullmatch(r'[-a-zA-Z0-9,._+#@]+', next_tok):
                delete_prop(node, next_tok)
                match(ps, next_tok)
                next_tok = match(ps, ';')
            continue
        elif next_tok == '#include':
            next_tok = match(ps, next_tok)
            set_add(S.cur_dt.includes, next_tok)
            next_tok = match(ps, next_tok)
            continue

        while next_tok is not None and re.fullmatch(r'(\w+):', next_tok):
            childlabels.append(next_tok[:-1])
            if S.trace_parse:
                print(f"[Label: {next_tok[:-1]}]")
            next_tok = match(ps, next_tok)

        if next_tok is not None and re.fullmatch(r'[-a-zA-Z0-9,._+#@]+', next_tok):
            name = next_tok
            if re.search(r'@0[0-9a-fA-F]', name):
                print(f"* Leading zero in node name '{name}'")
            next_tok = match(ps, next_tok)
            if next_tok == '{':
                next_tok = parse_node(ps, node, depth + 1, name, *childlabels)
            elif next_tok == '=':
                prop = []
                if childlabels and S.warnings:
                    print(f"* Ignoring label on property '{name}'")
                while True:
                    next_tok = match(ps, next_tok)
                    m = re.fullmatch(r'"(.*)"', next_tok) if next_tok is not None else None
                    if m:
                        prop.append(['"', m.group(1)])
                        next_tok = match(ps, next_tok)
                    elif next_tok is not None and re.fullmatch(r'&(.*)', next_tok):
                        prop.append(['&', next_tok[1:]])
                        next_tok = match(ps, next_tok)
                    elif next_tok == '<' or next_tok == '/bits/':
                        elemsize = 4
                        if next_tok == '/bits/':
                            next_tok = match(ps, next_tok)
                            if next_tok not in ('8', '16', '32', '64'):
                                raise OvMergeError(f"* Invalid /bits/ value '{next_tok}'.")
                            elemsize = int(next_tok) // 8
                            match(ps, next_tok)
                        next_tok = match(ps, '<')
                        vals = []
                        while next_tok != '>':
                            vals.append(next_tok)
                            next_tok = match(ps, next_tok)
                        prop.append(['<', elemsize, vals])
                        next_tok = match(ps, '>')
                    else:
                        vals = []
                        next_tok = match(ps, '[')
                        while next_tok != ']':
                            vals.extend(byte_array_value(next_tok))
                            next_tok = match(ps, next_tok)
                        next_tok = match(ps, ']')
                        prop.append(['[', 1, vals])
                    if next_tok != ',':
                        break
                next_tok = match(ps, ';')
                set_prop(node, name, *prop)
            else:
                if childlabels and S.warnings:
                    print(f"* Ignoring label on property '{name}'")
                next_tok = match(ps, ';')
                set_prop(node, name)
        else:
            raise OvMergeError(f"* Unexpected token '{next_tok}'")

    labels = S.cur_dt.labels
    for newlabel in newlabels:
        labelled_node = labels.get(newlabel)
        if labelled_node is not None:
            if labelled_node is not node:
                print(f"* Duplicated label '{newlabel}' - '{labelled_node[NAME]}' and '{node[NAME]}'",
                      file=sys.stderr)
            else:
                if S.warnings:
                    print(f"* Replicated label '{newlabel}' (on the same node)")
        add_label(S.cur_dt, node, newlabel)

    match(ps, '}')
    return match(ps, ';')


def dtparse(filename, got_header):
    defines = {}
    tokens = read_tokens(filename, 0, defines)
    ps = PState(tokens)

    dt = DT()
    dt.defines = defines

    next_tok = get_head(ps)

    while next_tok is not None and re.fullmatch(r'/.+/|#include', next_tok):
        typ = next_tok
        next_tok = match(ps, next_tok)
        if typ == '#include':
            if S.trace_parse:
                print(f"[#include {next_tok}]")
            set_add(dt.includes, next_tok)
            next_tok = match(ps, next_tok)
        else:
            if not got_header:
                if typ != '/dts-v1/':
                    raise OvMergeError("* File missing /dts-v1/ tag")
                got_header = True
            elif typ == '/dts-v1/':
                if S.warnings:
                    print("* Ignoring duplicate /dts-v1/ tag")
            elif typ == '/plugin/':
                dt.plugin = True
            elif typ == '/memreserve/':
                start = get_int(ps)
                length = get_int(ps)
                set_add(dt.memreserves, [start, length])
            else:
                raise OvMergeError(f"* Unexpected token '{typ}'")
            next_tok = match(ps, ';')

    S.cur_dt = dt

    while next_tok is not None:
        if next_tok == '/':
            match(ps, '/')
            next_tok = parse_node(ps, None, 0, '/')
        else:
            newlabels = []
            while next_tok is not None and re.fullmatch(r'(\w+):', next_tok):
                newlabels.append(next_tok[:-1])
                if S.trace_parse:
                    print(f"[Label: {next_tok[:-1]}]")
                next_tok = match(ps, next_tok)

            m = re.fullmatch(r'&(\w+)', next_tok) if next_tok is not None else None
            if m:
                label = m.group(1)
                subnode = dt.labels.get(label)
                match(ps, next_tok)
                if subnode is not None:
                    next_tok = parse_node(ps, subnode[PARENT], subnode[DEPTH], subnode, *newlabels)
                else:
                    print(f"* Unknown label '{label}'", file=sys.stderr)
                    next_tok = parse_node(ps, None, 0, '/', *newlabels)
            elif next_tok == '/delete-node/':
                next_tok = match(ps, next_tok)
                m2 = re.fullmatch(r'&(\w+)', next_tok) if next_tok is not None else None
                if m2:
                    label = m2.group(1)
                    subnode = dt.labels.get(label)
                    match(ps, next_tok)
                    if subnode is not None:
                        delete_node(subnode)
                    else:
                        print(f"* Unknown label '{label}'", file=sys.stderr)
                    next_tok = match(ps, ';')
            elif next_tok == '#include':
                next_tok = match(ps, next_tok)
                set_add(dt.includes, next_tok)
                next_tok = match(ps, next_tok)
            else:
                raise OvMergeError(f"* Unexpected token '{next_tok}'")

    S.cur_dt = None

    if ps.pos != len(ps.tokens):
        print(f"* Junk at the end - {get_head(ps)} ...")

    for key, value in list(dt.refcount.items()):
        if value < 0:
            raise OvMergeError(f"* Internal error - negative refcount on '{key}'")
        elif value > 0 and key not in dt.labels and not dt.plugin:
            print(f"* symbol '{key}' is undefined in '{filename}'", file=sys.stderr)
            S.retcode = 1

    if dt.plugin:
        ordercheck(dt, {})

    overrides = get_node(dt, '/__overrides__')
    for param in (overrides[PROPS] if overrides is not None else []):
        dtparam(dt, param[0], None)

    return dt
