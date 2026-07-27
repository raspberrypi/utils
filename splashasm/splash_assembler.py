#!/usr/bin/env python3
#
# Based on the panel-mipi-dbi language by Noralf Trønnes <noralf@tronnes.org>, 2022
# https://github.com/notro/panel-mipi-dbi
#
# Extended by Thomas Griffiths <thomas.griffiths@raspberrypi.com>
#

from typing import List, Self, Optional, Tuple
import ast
from abc import ABC, abstractmethod
from dataclasses import dataclass, fields
from pathlib import Path
from enum import Enum
import argparse
import sys

FILE_DIR = ""
MAGIC = b'SPLASH ASM\x00\x00\x00\x00\x00'

class WireProtocols(Enum):
    I2C = "i2c"
    SPI = "spi"

class Instructions(Enum):
    DELAY = b'\x00'
    DEFINE = b'\x01'
    COMMAND = b'\x10'

class SPI_FLAGS(Enum):
    DATA_ONLY = b'\x01'
    SWALLOW_ERRORS = b'\x80'

class I2C_FLAGS(Enum):
    READ = b'\x01'
    WRITE = b'\x00'
    SWALLOW_ERRORS = b'\x80'

class DELAY_UNITS(Enum):
    US = 1
    MS = 1000
    S  = 1000000

@dataclass(frozen=True, eq=False)
class Param:
    default: Optional[int]
    allowed_values: Optional[Tuple[int, ...]] = None

    def check(self, value: int) -> bool:
        return self.allowed_values is None or value in self.allowed_values

class SPI_PARAMS(Enum):
    COPI: Param = Param(default=10, allowed_values=(10,38))
    CIPO: Param = Param(default=9, allowed_values=(9,37))
    CS: Param = Param(default=8)
    SCLK: Param = Param(default=11)
    DC: Param = Param(default=None)
    CPOL: Param = Param(default=1, allowed_values=(1, 2))
    CPHA: Param = Param(default=1, allowed_values=(1, 2))
    CSPOL: Param = Param(default=1, allowed_values=(1, 2))
    FREQ: Param = Param(default=25000000)

class I2C_PARAMS(Enum):
    SDA: Param = Param(default=2, allowed_values=(2,))
    SCL: Param = Param(default=3, allowed_values=(3,))
    ADDR: Param = Param(default=0xFF)
    FREQ: Param = Param(default=100000)

class State:
    lines: str
    file_name: str

    def __init__(self):
        self.consts: List[Constant] = []
        self.outs: List[Define] = []
        self.curr_idx: int = 0
        self.parsed_lines: List[Instruction] = []

    def at_end(self) -> bool:
        return self.curr_idx >= len(self.lines)

    def line_number(self, idx : int = None) -> int:
        return self.lines[:self.curr_idx if idx is None else idx].count('\n') + 1

    def line_remainder(self, idx : int = None) -> str:
        """Returns the remainder of the line the state is currently on"""
        idx = self.curr_idx if idx is None else idx
        end = self.lines.find('\n', idx)
        if end == -1:
            end = len(self.lines)
        return self.lines[idx:end]

    def swallow_empty_chars(self) -> bool:
        any_empty = False
        while self.curr_idx < len(self.lines) and (self.lines[self.curr_idx] == ' ' or self.lines[self.curr_idx] == '\n'):
            self.curr_idx += 1
            any_empty = True
        return any_empty

    def read_nonempty_chars(self) -> str:
        accumulator = ""
        while self.curr_idx < len(self.lines) and self.lines[self.curr_idx] != ' ' and self.lines[self.curr_idx] != '\n':
            accumulator += self.lines[self.curr_idx]
            self.curr_idx += 1

        return accumulator

    def read_token(self) -> str:
        """Like read_nonempty_chars, but can also take in parenthesised things with empty chars inside"""
        open_c = self.lines[self.curr_idx]
        close_c = {'(': ')', '[': ']'}.get(open_c)
        if close_c is None:
            return self.read_nonempty_chars()

        start = self.curr_idx
        depth = 0
        while True:
            if self.curr_idx >= len(self.lines):
                raise ValueError(f"unterminated '{open_c}'")
            c = self.lines[self.curr_idx]
            if c == open_c:
                depth += 1
            elif c == close_c:
                depth -= 1
            self.curr_idx += 1
            if depth == 0:
                break

        return self.lines[start:self.curr_idx]
    
    def is_at_an_instruction(self):
        return any([cls.is_parsable(self) for cls in Instruction.__subclasses__()])

    def eval_const(self, expr):
        tree = ast.parse(expr, mode='eval')

        vars = {c.name : c.int_value for c in self.consts}
        allowed_names = set(vars.keys())

        for node in ast.walk(tree):
            if isinstance(node, ast.Name):
                if node.id not in allowed_names:
                    raise ValueError(f"Unknown identifier: {node.id!r}")
                if vars[node.id] is None:
                    raise ValueError(f"{node.id!r} is not defined yet (used before its definition, circular, or an extern that hasn't been resolved)")
            elif isinstance(node, (ast.Call, ast.Attribute, ast.Subscript,
                                    ast.Lambda, ast.Import, ast.ImportFrom)):
                raise ValueError(f"Disallowed call in preprocessing: {type(node).__name__}")

        return eval(compile(tree, '<const_expr>', 'eval'), vars)

    def eval_consts(self):
        for const in self.consts:
            try:
                if const.value is None:
                    continue

                const.int_value = int(self.eval_const(const.value))
            except Exception as e:
                raise type(e)(f"file {const.file_name} line {const.start_line} evaluating const {const.name}: {e}") from e

    def parse_val(self, val : str) -> int:
        if val[0] == '(' and val[-1] == ')':
            val = val[1:-1]
        try:
            names = {const.name : const.int_value for const in self.consts}
            names['__builtins__'] = {}
            return eval(val, names)
        except NameError as n:
            raise ValueError(f"{n.name} was not defined")
        
    def get_command_instruction_code(self, command):
        """Gets an id for a define in a command, this is just the order in which the commands were defined"""
        index = self.outs.index(command)

        if index is None:
            raise ValueError("You have used an undefined command directive")

        if index != index & 0xff:
            raise ValueError("You have defined too many outputs, cannot fit output code into a byte")

        return index.to_bytes(1)[0]

class Instruction(ABC):
    start_line: str
    file_name: str

    @staticmethod
    def is_parsable(state: State) -> bool:
        ...

    @classmethod
    def parse(cls, state: State) -> Self:
        """Parses out the string from state into some instruction object"""
        start_idx = state.curr_idx
        line = state.lines[:start_idx].count('\n') + 1
        try:
            if not cls.is_parsable(state):
                raise ValueError("This is the wrong class for parsing this line")
            ret_val = cls._do_parse(state)
            ret_val.start_line = line
            ret_val.file_name = state.file_name
            return ret_val
        except Exception as e:
            raise type(e)(f"file {state.file_name} line {line} parsing {cls.__name__}: {e}") from e

    @classmethod
    def _do_parse(cls, state: State) -> Self:
        ...

    def emit_binary(self, state: State, arr_ptr: bytearray) -> None:
        try:
            self._do_emit_binary(state, arr_ptr)
        except Exception as e:
            raise type(e)(f"file {self.file_name} line {self.start_line} emitting binary {type(self).__name__}: {e}") from e

    def _do_emit_binary(self, state: State, arr_ptr: bytearray) -> None:
        """Turns self into a binary and puts it on the end of arr_ptr"""
        ...

class Constant(Instruction):
    name: str = ""
    is_extern: bool = False
    value: str = None
    int_value: int = None

    @staticmethod
    def is_parsable(state):
        return state.lines.startswith("const", state.curr_idx)
    
    @classmethod
    def _do_parse(cls, state) -> Self:
        const = Constant()

        state.read_nonempty_chars()

        state.swallow_empty_chars()
        const.name = state.read_nonempty_chars()
        
        state.swallow_empty_chars()
        val = state.read_token()

        if val == "extern":
            const.is_extern = True
        else:
            try:
                const.int_value = int(val, 0) # auto detects base
            except ValueError:
                const.value = val

        return const
    
class Define(Instruction):
    name: str
    protocol_type: WireProtocols

    def __init__(self):
        self.is_extern: bool = False
        self.params: List[str] = []

    @staticmethod
    def is_parsable(state):
        return state.lines.startswith("define", state.curr_idx)

    @classmethod
    def _do_parse(cls, state):
        define = Define()

        state.read_nonempty_chars()
        state.swallow_empty_chars()

        define.name = state.read_nonempty_chars()
        state.swallow_empty_chars()

        protocol_type = state.read_nonempty_chars().lower()

        try:
            define.protocol_type = WireProtocols(protocol_type)
        except ValueError:
            raise ValueError(f"Unknown wire protocol {protocol_type}")

        # register before parsing our own params, so a Command targeting this
        # bus is recognisable by is_at_an_instruction() even with nothing
        # else between the define line and the first command using it
        state.outs.append(define)

        state.swallow_empty_chars()

        if state.lines.startswith("extern", state.curr_idx):
            state.read_nonempty_chars()
            define.is_extern = True
            return define

        while not state.is_at_an_instruction() and not state.at_end():
            define.params.append(state.read_token())
            state.swallow_empty_chars()
        return define

    def _do_emit_binary(self, state, arr_ptr):
        arr_ptr.extend(Instructions.DEFINE.value)

        arr_ptr.extend(bytearray("SPI " if self.protocol_type == WireProtocols.SPI else "I2C ", 'ascii'))

        values = {}
        for param in self.params:
            name, value = param.strip('[]').split()
            values[name.upper()] = state.parse_val(value)

        if self.protocol_type == WireProtocols.SPI:
            default_params = SPI_PARAMS
        elif self.protocol_type == WireProtocols.I2C:
            default_params = I2C_PARAMS

        for p in default_params:
            if p.name not in values.keys() and p.value.default is not None:
                print(f"Didn't specify nessercary param {p.name} in {self.__class__.__name__} on in file {self.file_name} on line {self.start_line}, defaulting to {p.value.default}")
                values[p.name] = p.value.default
            elif p.name in values.keys() and not p.value.check(values[p.name]):
                raise ValueError(f"{p.name} cannot be set to {values[p.name]}, the only valid options are {", ".join(str(v) for v in p.value.allowed_values)}")
            elif p.name not in values.keys() and p.value.default is None:
                values[p.name] = 0xFF


        if self.protocol_type == WireProtocols.SPI:
            packed = bytes([values["COPI"], values["CIPO"], values["SCLK"], values["CS"],
                             values["DC"], values["CPOL"],
                             values["CPHA"], values["CSPOL"]])
            packed += values["FREQ"].to_bytes(4, byteorder='little')
        else:
            packed = bytes([values["SDA"], values["SCL"], values["ADDR"], 0])
            packed += values["FREQ"].to_bytes(4, byteorder='little')

        arr_ptr.append(len(packed))
        arr_ptr.extend(b'\x00\x00\x00')  # reserved (struct padding after param_len)
        arr_ptr.extend(packed)

class Delay(Instruction):
    value: str
    unit: str = None

    @staticmethod
    def is_parsable(state):
        return state.lines.startswith("delay", state.curr_idx)
    
    @classmethod
    def _do_parse(cls, state):
        delay = Delay()

        state.read_nonempty_chars()
        state.swallow_empty_chars()

        if state.lines.startswith("[", state.curr_idx):
            delay.unit = state.read_token()[1:-1].upper()
            if delay.unit not in DELAY_UNITS._member_names_:
                raise ValueError(f"Delay Unit {delay.unit} not a valid time unit (only {DELAY_UNITS._member_names_} are valid)")
            state.swallow_empty_chars()

        delay.value = state.read_nonempty_chars()

        return delay
    
    def __get_value(self, state) -> int:
        return state.parse_val(self.value)
    
    def _do_emit_binary(self, state, arr_ptr):
        arr_ptr.extend(Instructions.DELAY.value)

        value = self.__get_value(state)
        if self.unit is not None:
            value *= DELAY_UNITS[self.unit].value

        arr_ptr.extend(value.to_bytes(4, byteorder="little"))

class Command(Instruction):
    define: Define

    def __init__(self):
        self.data: List[str] = []
        self.flags: List[str] = []

    @staticmethod
    def is_parsable(state):
        return any(state.lines.startswith(define.name, state.curr_idx) for define in state.outs)

    @classmethod
    def _do_parse(cls, state):
        cmd = Command()
        name = state.read_nonempty_chars()

        define = next((s for s in state.outs if s.name == name), None)

        if define is None:
            raise ValueError(f"Command {name} couldn't link with its define")

        cmd.define = define
        state.swallow_empty_chars()

        while not state.at_end() and not state.is_at_an_instruction():
            read = state.read_token()
            if read[-1] == ']' and read[0] == '[':
                cmd.flags.append(read)
            elif read[-1] == ')' and read[0] == '(':
                # an expression to be resolved against the protocol's flag enum
                # when we emit binary, since it may reference names not yet in scope here
                cmd.data.append(read)
            else:
                try:
                    cmd.data.append(int(read, 16).to_bytes(1, 'little'))
                except ValueError:
                    cmd.data.append(read)

            state.swallow_empty_chars()
        return cmd
        
    def _parse_flags(self, state) -> int:
        protocol_flags = None
        if self.define.protocol_type == WireProtocols.SPI:
            protocol_flags = SPI_FLAGS
        elif self.define.protocol_type == WireProtocols.I2C:
            protocol_flags = I2C_FLAGS
        
        returnValue = 0
        for f in self.flags:
            if f[0] != '[' or f[-1] != ']':
                continue

            try:
                returnValue |= int.from_bytes(protocol_flags[f[1:-1]].value, byteorder='little')
            except KeyError:
                raise ValueError(f"Invalid flag {f} for protocol type {self.define.protocol_type.name}")

        return returnValue.to_bytes(1, byteorder="little")
        
    def _do_emit_binary(self, state, arr_ptr):
        command_index = state.get_command_instruction_code(self.define)
        if command_index is None:
            raise ValueError(f"Command {self.define.name} is not properly linked")

        arr_ptr.extend(Instructions.COMMAND.value)
        arr_ptr.append(command_index)

        arr_ptr.extend(self._parse_flags(state))

        data_bytes = bytearray()
        for item in self.data:
            if isinstance(item, (bytes, bytearray)):
                data_bytes.extend(item)
            else:
                data_bytes.extend(state.parse_val(item).to_bytes(1, byteorder='little'))

        arr_ptr.extend(len(data_bytes).to_bytes(2, byteorder='little'))
        arr_ptr.extend(data_bytes)

        

"""this is again for the compiler so it doesn't have a representation"""
class Import(Instruction):
    linking_file: str

    @staticmethod
    def is_parsable(state):
        return state.lines.startswith("import", state.curr_idx)
    
    @classmethod
    def _do_parse(cls, state):
        imp = Import()
        
        state.read_nonempty_chars()
        state.swallow_empty_chars()

        imp.linking_file = state.read_nonempty_chars()

        return imp
    
def strip_line(line : str) -> str:
    comment_idx = line.find('#')
    if comment_idx >= 0:
        line = line[:comment_idx]
    return line.strip()

def strip_lines(lines : List[str]) -> List[str]:
    return [strip_line(line) for line in lines]

def generate_ast(file_name : str) -> State:
    with open(FILE_DIR.parent / file_name, mode='r') as f:
        lines = f.readlines()

    lines = strip_lines(lines)

    data_blob = "\n".join(lines)

    state = State()
    state.lines = data_blob
    state.file_name = file_name

    while not state.at_end():
        while state.swallow_empty_chars():
            continue
        opts = [cls for cls in Instruction.__subclasses__() if cls.is_parsable(state)]
        if len(opts) == 0:
            raise ValueError(f"Unparsable line at line {state.line_number()}: {state.line_remainder()}")

        if len(opts) != 1:
            raise ValueError(f"Ambiguous grammar at line {state.line_number()} this could parse as any of the following: {" ".join(o.__name__ for o in opts)}")

        new_instruction = opts[0].parse(state)

        if isinstance(new_instruction, Define):
            pass  # already registered by Define._do_parse
        elif isinstance(new_instruction, Constant):
            state.consts.append(new_instruction)
        else:
            state.parsed_lines.append(new_instruction)

    return state

def _collect_states(state : State, all_states):
    linked_ast = []
    parsed_files = [state.file_name]
    
    for inst in state.parsed_lines:
        if isinstance(inst, Import):
            if inst.linking_file in parsed_files:
                raise ValueError(f"file {inst.file_name} line {inst.start_line}: Recursive link back to {inst.linking_file} when trying to link imports")

            imported_state = generate_ast(inst.linking_file.strip("\""))
            all_states.append(imported_state)
            
            linked_ast.extend(_collect_states(imported_state, all_states))
            parsed_files.append(inst.linking_file.strip("\""))
        else:
            linked_ast.append(inst)

    return linked_ast

def get_defined_externs(matchables, extern):
    matches = [matchable for matchable in matchables
                   if not matchable.is_extern and matchable.name == extern.name
                   and getattr(matchable, "protocol_type", None) == getattr(extern, "protocol_type", None)]

    if len(matches) == 0:
        raise ValueError(f"No matches for extern {type(extern).__name__} {extern.name}")

    if len(matches) != 1:
        raise ValueError(f"{len(matches)} matches for extern {type(extern).__name__} {extern.name}")
    return matches

def do_linking(main_state : State):
    all_states = [main_state]

    linked_ast = _collect_states(main_state, all_states)

    extern_consts = [const for state in all_states for const in state.consts if const.is_extern]

    extern_outs = [out for state in all_states for out in state.outs if out.is_extern]

    for extern in extern_consts:
        matches = get_defined_externs([const for state in all_states for const in state.consts], extern)

        extern.is_extern = False
        extern.value = matches[0].value
        extern.int_value = matches[0].int_value

    for extern in extern_outs:
        matches = get_defined_externs([out for state in all_states for out in state.outs], extern)

        extern.is_extern = False
        extern.params = matches[0].params

        for inst in linked_ast:
            if isinstance(inst, Command) and inst.define is extern:
                inst.define = matches[0]

    resolved_externs = set(id(e) for e in extern_outs)
    main_state.outs = [out for state in all_states for out in state.outs
                        if id(out) not in resolved_externs]

    resolved_consts = set(id(e) for e in extern_consts)
    main_state.consts = [const for state in all_states for const in state.consts
                        if id(const) not in resolved_consts]

    main_state.parsed_lines = linked_ast
    return main_state

def pretty_print(data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part = ' '.join(f'{b:02x}' for b in chunk)
        lines.append(f'{i:08x}  {hex_part}')
    return '\n'.join(lines)

def make_file(input_file):
    buf = bytearray()
    buf.extend(MAGIC) # magic
    buf.append(1) # version

    buf.extend(compile_file(input_file))

    return buf

def compile_file(input_file):
    state = generate_ast(input_file)

    do_linking(state)

    state.eval_consts()

    return_bytes = bytearray()
    if len([d for d in state.outs if d.protocol_type == WireProtocols.SPI]) > 4:
        raise ValueError("The driver does not support more than four spi handles")
    
    if len([d for d in state.outs if d.protocol_type == WireProtocols.I2C]) > 10:
        raise ValueError("The driver does not support more than ten i2c handles")

    for define in state.outs:
        define.emit_binary(state, return_bytes)

    for inst in state.parsed_lines:
        inst.emit_binary(state, return_bytes)

    return return_bytes

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Splash Screen Assembler for the Raspberry Pi Bootloader')
    parser.add_argument('input', nargs='?', help='Input .splash file')
    parser.add_argument('-o', '--output', help='Output binary file')
    parser.add_argument('-p', '--pretty-print', action='store_true', help='Prints out the binary as nicely formatted hex')
    args = parser.parse_args()

    if len(sys.argv) == 1:
        parser.print_help(sys.stderr)
        sys.exit(1)

    FILE_DIR = Path(args.input)

    try:
        if args.input:
            buf = make_file(Path(args.input).name)
            
            if args.output:
                with open(args.output, mode='wb') as f:
                    f.write(buf)
            else:
                if args.pretty_print:
                    print(pretty_print(buf))
                else:
                    sys.stdout.buffer.write(buf)

    except Exception as e:
        print(f"\033[91m{e}\033[0m", file=sys.stderr)
        sys.exit(1)
