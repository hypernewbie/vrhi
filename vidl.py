#
#    -- Vidl --
#
#    Copyright 2026 UAA Software
#
#    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
#    associated documentation files (the "Software"), to deal in the Software without restriction,
#    including without limitation the rights to use, copy, modify, merge, publish, distribute,
#    sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
#    furnished to do so, subject to the following conditions:
#
#    The above copyright notice and this permission notice shall be included in all copies or substantial
#    portions of the Software.
#
#    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
#    NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
#    OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

# WARNING: Vibe code generation tool ahead. Read at your own risk of sanity.

import os, sys, re, hashlib

def generate_magic(name):
    return "0x" + hashlib.sha256(name.encode()).hexdigest()[:8].upper()

CPP_KEYWORDS = {
    'alignas', 'alignof', 'and', 'and_eq', 'asm', 'atomic_cancel', 'atomic_commit',
    'atomic_noexcept', 'auto', 'bitand', 'bitor', 'bool', 'break', 'case', 'catch',
    'char', 'char8_t', 'char16_t', 'char32_t', 'class', 'compl', 'concept',
    'const', 'consteval', 'constexpr', 'constinit', 'const_cast', 'continue',
    'co_await', 'co_return', 'co_yield', 'decltype', 'default', 'delete', 'do',
    'double', 'dynamic_cast', 'else', 'enum', 'explicit', 'export', 'extern',
    'false', 'float', 'for', 'friend', 'goto', 'if', 'inline', 'int', 'long',
    'mutable', 'namespace', 'new', 'noexcept', 'not', 'not_eq', 'nullptr',
    'operator', 'or', 'or_eq', 'private', 'protected', 'public', 'reflexpr',
    'register', 'reinterpret_cast', 'requires', 'return', 'short', 'signed',
    'sizeof', 'static', 'static_assert', 'static_cast', 'struct', 'switch',
    'synchronized', 'template', 'this', 'thread_local', 'throw', 'true', 'try',
    'typedef', 'typeid', 'typename', 'union', 'unsigned', 'using', 'virtual',
    'void', 'volatile', 'wchar_t', 'while', 'xor', 'xor_eq'
}

def sanitize_name(name):
    if name in CPP_KEYWORDS:
        return name + "_"
    return name

def parse_function(text):
    # Regex to match function signature: return_type name ( params )
    # Use greedy match (.*) for params to capture nested parentheses like TestStruct()
    # assuming the input text ends near the closing parenthesis of the function.
    match = re.match(r'\s*([\w:]+(?:\s*\*+)?)\s+(\w+)\s*\((.*)\)', text, re.DOTALL)
    if not match:
        return None
    
    return_type = match.group(1).strip()
    name = match.group(2).strip()
    params_text = match.group(3).strip()
    
    params = []
    if params_text:
        # Split params by comma
        raw_params = re.split(r',', params_text)
        for p in raw_params:
            p = p.strip()
            if not p: continue
            
            # Handle default values
            parts_eq = p.split('=')
            p_clean = parts_eq[0].strip()
            default_val = parts_eq[1].strip() if len(parts_eq) > 1 else None
            
            # Split type and name
            parts = p_clean.split()
            if len(parts) >= 2:
                p_name = parts[-1]
                p_type = ' '.join(parts[:-1])
                # Handle cases like uint32_t* x
                if p_name.startswith('*'):
                    p_type += '*'
                    p_name = p_name[1:]
                params.append({
                    'type': p_type, 
                    'name': sanitize_name(p_name), 
                    'orig_name': p_name,
                    'default': default_val
                })
    
    return {
        'return_type': return_type,
        'name': name,
        'params': params
    }

def generate_source(content):
    # Find all // VIDL_GENERATE
    # Then find the function signature that follows
    # We look for // VIDL_GENERATE and then everything until a semicolon
    pattern = r'//\s*VIDL_GENERATE(.*?);'
    matches = re.findall(pattern, content, re.DOTALL)

    generated_structs = []
    handler_funcs = []

    for m in matches:
        # Strip comments from the captured block
        m = re.sub(r'//.*', '', m)
        m = re.sub(r'/\*.*?\*/', '', m, flags=re.DOTALL)
        
        func = parse_function(m)
        if not func:
            continue
        
        magic = generate_magic(func['name'])
        handler_funcs.append({'name': func['name'], 'magic': magic})
        
        # Generate struct
        struct_lines = [f"struct VIDL_{func['name']}", "{"]
        struct_lines.append(f"    static constexpr uint64_t kMagic = {magic};")
        struct_lines.append("    uint64_t MAGIC = kMagic;")
        
        ctor_params = []
        initializer_list = []
        
        for p in func['params']:
            default_str = f" = {p['default']}" if p.get('default') else ""
            struct_lines.append(f"    {p['type']} {p['name']}{default_str};")
            # For constructor: type _name
            ctor_params.append(f"{p['type']} _{p['name']}")
            # For initializer: name(_name)
            initializer_list.append(f"{p['name']}(_{p['name']})")
        
        # Default constructor
        struct_lines.append("")
        struct_lines.append(f"    VIDL_{func['name']}() = default;")
        
        # Parameterized constructor
        if ctor_params:
            struct_lines.append("")
            struct_lines.append(f"    VIDL_{func['name']}({', '.join(ctor_params)})")
            struct_lines.append(f"        : {', '.join(initializer_list)} {{}}")

        struct_lines.append("};")
        generated_structs.append("\n".join(struct_lines))

    # Generate Handler
    handler_lines = ["struct VIDLHandler", "{"]
    for h in handler_funcs:
        handler_lines.append(f"    virtual void Handle_{h['name']}( VIDL_{h['name']}* cmd ) {{ (void) cmd; }};")
    
    handler_lines.append("")
    handler_lines.append("    virtual void HandleCmd( void* cmd )")
    handler_lines.append("    {")
    handler_lines.append("        uint64_t magic = *(uint64_t*)cmd;")
    handler_lines.append("        switch ( magic )")
    handler_lines.append("        {")
    
    for h in handler_funcs:
        handler_lines.append(f"        case {h['magic']}:")
        handler_lines.append(f"            Handle_{h['name']}( (VIDL_{h['name']}*) cmd );")
        handler_lines.append("            break;")
        
    handler_lines.append("        }")
    handler_lines.append("    }")
    handler_lines.append("};")

    output = []
    output.append("// Generated by Vidl - DO NOT MODIFY : see vidl.py")
    output.append("#include <cstdint>")
    output.append("")
    output.append("\n\n".join(generated_structs))
    output.append("")
    output.append("\n".join(handler_lines))
    
    return "\n".join(output)

def main():
    if len(sys.argv) < 2:
        print("Usage: python vidl.py <input_file> [output_file]")
        return

    input_file = sys.argv[1]
    with open(input_file, 'r', encoding='utf-8') as f:
        content = f.read()

    result = generate_source(content)
    
    if len(sys.argv) >= 3:
        output_file = sys.argv[2]
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(result)
    else:
        print(result)

if __name__ == "__main__":
    main()
