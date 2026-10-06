"""Decode selected pinned GCL bodies; retain the complete record and byte evidence."""
import argparse
import hashlib
import json
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

parser = argparse.ArgumentParser()
parser.add_argument('gcl', type=Path)
parser.add_argument('records', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
blob = args.gcl.read_bytes()
expected = '8b877a19fea261e0524a4cea39beb89a200796dfc74229a2bd13f884d2d01d4a'
if hashlib.sha256(blob).hexdigest() != expected:
    raise SystemExit('Pinned GCL hash mismatch; evidence must be re-audited')
records = json.loads(args.records.read_text())
indices = [4901, 5045, 5046, 6299, 6300, 6326, 6327, 14072, 14073, 35751,
           38622, 41348, 41351, 41374, 41375, 48768, 48769]
needles = ['server_scene.item_world.get_', 'client_scene.item_world.get_',
           'client_scene.inventory.$ctor', 'server_scene.inventory.$ctor',
           'client_ui.begin', 'client_ui.render', 'client_ui_renderer.render',
           'client_ui_renderer.begin', 'client_scene.actor_character.get_inventory',
           'server_scene.actor_character.get_inventory', 'renderer.get_camera',
           'application.graphics.camera.get_screen', 'get_stack_count', 'get_stack_capacity',
           'client_scene.vehicle_component.get_transform', 'server_scene.vehicle_component.get_transform']
indices += [r['index'] for r in records if any(n in r['signature'] for n in needles)]
decoder = Cs(CS_ARCH_X86, CS_MODE_64)
result = []
for index in sorted(set(indices)):
    record = records[index]
    body = blob[record['body_offset']:record['body_offset'] + record['code_size']]
    result.append(dict(record=record, body_sha256=hashlib.sha256(body).hexdigest(),
                       bytes=body.hex(), disassembly=[dict(offset=i.address,
                       bytes=i.bytes.hex(), instruction=i.mnemonic + ' ' + i.op_str)
                       for i in decoder.disasm(body, 0)]))
args.output.write_text(json.dumps(dict(gcl_sha256=expected, functions=result), indent=2))
patterns=['#pragma once', '// Generated from the pinned GCL; exact full-body matching is required.']
for index,name in [(14072,'CLIENT_COMPONENT_WORLD'),(14073,'CLIENT_COMPONENT_GRID'),(48768,'SERVER_COMPONENT_WORLD'),(48769,'SERVER_COMPONENT_GRID')]:
    record=records[index];body=blob[record['body_offset']:record['body_offset']+record['code_size']]
    patterns.append('static const unsigned char P29_'+name+'[]={'+','.join('0x%02x'%b for b in body)+'};')
(args.output.parent/'phase29_transform_patterns.h').write_text('\n'.join(patterns)+'\n')
print(f'Retained {len(result)} pinned native records in {args.output}')
