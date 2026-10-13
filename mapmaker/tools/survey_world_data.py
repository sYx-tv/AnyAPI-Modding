"""Summarise how the installed game stores its world, for map-editor development.

Usage: python mapmaker/tools/survey_world_data.py [game_dir] [out.json]

Reads <game>/rom only and writes a compact JSON report (schemas, id lists, value ranges and two
trimmed example tiles). It copies no meshes or textures; the report is small enough to paste.
"""
import collections, json, re, sys
from pathlib import Path

game = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r'C:\Program Files (x86)\Steam\steamapps\common\Anymaker')
out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('world-survey.json')
rom = game / 'rom'
data = rom / 'data'
report = {'game': str(game)}


def kind(v):
    if isinstance(v, bool): return 'bool'
    if isinstance(v, int): return 'int'
    if isinstance(v, float): return 'float'
    if isinstance(v, str): return 'str'
    if isinstance(v, list): return 'list'
    if isinstance(v, dict): return 'dict'
    return 'null'


def schema(values, depth=0, limit=4):
    """Merge the shape of many JSON values into one description."""
    kinds = collections.Counter(kind(v) for v in values)
    node = {'kinds': dict(kinds)}
    if depth >= limit: return node
    dicts = [v for v in values if isinstance(v, dict)]
    if dicts:
        keys = collections.defaultdict(list)
        for d in dicts:
            for k, v in d.items(): keys[k].append(v)
        node['keys'] = {k: dict(schema(vs, depth + 1, limit), present=len(vs)) for k, vs in sorted(keys.items())}
    lists = [v for v in values if isinstance(v, list)]
    if lists:
        items = [x for l in lists for x in l][:20000]
        lengths = [len(l) for l in lists]
        node['length'] = [min(lengths), max(lengths)]
        if items: node['items'] = schema(items, depth + 1, limit)
    nums = [v for v in values if kind(v) in ('int', 'float')]
    if nums: node['range'] = [min(nums), max(nums)]
    strs = [v for v in values if isinstance(v, str)]
    if strs:
        c = collections.Counter(strs)
        node['distinct'] = len(c)
        node['top'] = c.most_common(12)
    return node


def trim(v, n=3):
    if isinstance(v, list): return [trim(x, n) for x in v[:n]] + ([f'... {len(v) - n} more'] if len(v) > n else [])
    if isinstance(v, dict): return {k: trim(x, n) for k, x in v.items()}
    return v


def load(p):
    try: return json.loads(p.read_text(encoding='utf-8'))
    except Exception as e: return {'__error__': str(e)}


# 1. Layout of rom/ (directories with counts, top-level data files with sizes).
layout = {}
for d in sorted(p for p in rom.rglob('*') if p.is_dir()):
    files = [f for f in d.iterdir() if f.is_file()]
    if files:
        exts = collections.Counter(f.suffix.lower() for f in files)
        layout[str(d.relative_to(rom)).replace('\\', '/')] = {'files': len(files), 'ext': dict(exts)}
report['rom_dirs'] = layout
report['data_files'] = {f.name: f.stat().st_size for f in sorted(data.iterdir()) if f.is_file()}
report['images_named_like_world'] = sorted(str(f.relative_to(rom)).replace('\\', '/') for f in rom.rglob('*')
    if f.is_file() and re.search(r'biome|world|map|terrain|height|ground|splat', f.name, re.I) and f.suffix.lower() in ('.png', '.dds', '.tga', '.bin', '.raw', '.json'))[:200]

# 2. Tiles: every folder under data/tiles, filename grid ranges, merged schema, examples.
tiles_root = data / 'tiles'
report['tile_folders'] = {}
tiles = []
for folder in sorted(p for p in tiles_root.iterdir() if p.is_dir()) if tiles_root.exists() else []:
    names = sorted(folder.glob('*.json'))
    xs, zs, ts = [], [], []
    for p in names:
        m = re.fullmatch(r'tile_(-?\d+)_(-?\d+)_(\d+)\.json', p.name)
        if m: xs.append(int(m[1])); zs.append(int(m[2])); ts.append(int(m[3]))
    report['tile_folders'][folder.name] = {'count': len(names), 'x': [min(xs), max(xs)] if xs else None,
        'z': [min(zs), max(zs)] if zs else None, 't': sorted(set(ts)), 'other_names': [p.name for p in names if not p.name.startswith('tile_')][:20]}
    if folder.name == '0':
        tiles = [(p.name, load(p)) for p in names]
report['tile_files_outside_folders'] = [p.name for p in tiles_root.glob('*') if p.is_file()][:50] if tiles_root.exists() else []
if tiles:
    report['tile_schema'] = schema([t for _, t in tiles], limit=5)
    with_roads = [t for t in tiles if t[1].get('junction_links')]
    with_props = [t for t in tiles if t[1].get('prop_spawn_zones')]
    plain = min(tiles, key=lambda t: len(json.dumps(t[1])))
    rich = max(with_roads or tiles, key=lambda t: len(t[1].get('prop_spawn_zones', [])) + len(t[1].get('spawn_zones', [])))
    report['example_tiles'] = {plain[0]: trim(plain[1]), rich[0]: trim(rich[1], 4)}
    # A few whole tiles so the editor's reader and writer can round-trip real files.
    picks = {plain[0]: plain[1], rich[0]: rich[1]}
    for group in (with_props, [t for t in tiles if t[1].get('pathway_junctions')], [t for t in tiles if t[1].get('loot_spawn_zones')]):
        if group: picks.setdefault(group[len(group) // 2][0], group[len(group) // 2][1])
    report['full_example_tiles'] = picks
    report['tile_counts'] = {'total': len(tiles), 'with_roads': len(with_roads), 'with_props': len(with_props),
        'with_spawn_zones': sum(1 for t in tiles if t[1].get('spawn_zones'))}
    # Occupancy grid (x,z,t -> definition, height, orientation) as compact rows, for drawing a preview.
    grid = []
    for name, t in tiles:
        m = re.fullmatch(r'tile_(-?\d+)_(-?\d+)_(\d+)\.json', name)
        if m: grid.append([int(m[1]), int(m[2]), int(m[3]), t.get('definition_name', ''), t.get('height', 0), t.get('orientation', 0)])
    defs = collections.Counter(g[3] for g in grid)
    report['tile_definition_usage'] = defs.most_common(60)
    report['tile_definition_count_used'] = len(defs)

# 3. Every other JSON in rom/data: shape only (depth 3), plus id lists for the world-related ones.
others = {}
ids = {}
for p in sorted(data.glob('*.json')):
    v = load(p)
    others[p.name] = schema([v], limit=3)
    text = p.name.lower()
    if any(k in text for k in ('tile', 'junction', 'prop', 'building', 'prefab', 'vegetation', 'biome', 'world', 'foundation', 'path', 'loot', 'spawn')):
        found = collections.OrderedDict()
        def walk(x, path):
            if isinstance(x, dict):
                for k in ('id', 'name', 'definition_name'):
                    if isinstance(x.get(k), str): found.setdefault(path, []).append(x[k])
                for k, y in x.items(): walk(y, path + '.' + k)
            elif isinstance(x, list):
                for y in x: walk(y, path + '[]')
        walk(v, '')
        ids[p.name] = {k: (vs if len(vs) <= 400 else vs[:400] + [f'... {len(vs) - 400} more']) for k, vs in found.items()}
report['data_json_shapes'] = others
report['world_related_ids'] = ids

# 4. Other world folders that may hold buildings or prefabs.
for name in ('buildings', 'prefabs', 'vehicles', 'structures', 'dungeons'):
    for d in rom.rglob(name):
        if d.is_dir():
            files = sorted(f for f in d.rglob('*') if f.is_file())
            report.setdefault('named_folders', {})[str(d.relative_to(rom)).replace('\\', '/')] = {
                'files': len(files), 'sample': [str(f.relative_to(d)).replace('\\', '/') for f in files[:40]]}

out.write_text(json.dumps(report, indent=1, default=str), encoding='utf-8')
print(f'Wrote {out} ({out.stat().st_size // 1024} KiB)')
