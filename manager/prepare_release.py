"""Create separate API/mod packages from verified revision25 DLLs; never package user data."""
from pathlib import Path
import argparse,hashlib,json,zipfile
root=Path(__file__).resolve().parent.parent
out=root/'manager/publishing';assets=out/'assets';assets.mkdir(parents=True,exist_ok=True)
evidence=json.loads((root/'release/DELIVERY_EVIDENCE.json').read_text())
game=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Anymaker')
sha=lambda data:hashlib.sha256(data).hexdigest()
assert all(sha((game/p).read_bytes())==v for p,v in evidence['game_identity_sha256'].items())
assert evidence['tests_passed']==27
build={'Version':evidence['game_version'],'SteamBuild':evidence['steam_build_id'],'ExeSha256':evidence['game_identity_sha256']['game.exe'],'GclSha256':evidence['game_identity_sha256']['bin/game.gcl']}
api=(game/'dinput8.dll').read_bytes();assert sha(api)==evidence['host_sha256']
names=[('anyapi','AnyAPI','The in-game framework. Mods are downloaded separately.','dinput8.dll',api),
 ('anyhelpers','AnyHelpers','Mod Controls and Mod Settings, built into the game settings.', 'AnyAPI and Modding/mods/AnyHelpers.dll',None),
 ('anyinventory','AnyInventory','Search the full item catalog with previews, filters and favorites.','AnyAPI and Modding/mods/AnyInventory.dll',None),
 ('anystorage','AnyStorage','Quick deposit, withdraw, matching stacks and storage sorting.','AnyAPI and Modding/mods/AnyStorage.dll',None),
 ('anymap','AnyMap','World map, smooth minimap, named markers and road guidance.','AnyAPI and Modding/mods/AnyMap.dll',None)]
catalog={'Schema':1,'Api':[],'Mods':[]}
for ident,name,desc,target,data in names:
 data=data or (game/target).read_bytes()
 if ident=='anymap':assert sha(data)==evidence['map_sha256']
 path=assets/(name+'-0.25.0.zip')
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:z.writestr(target,data)
 with zipfile.ZipFile(path) as z:assert z.namelist()==[target] and z.read(target)==data
 entry={'Id':ident,'Name':name,'Version':'0.25.0','Revision':25,'MinimumApi':0 if ident=='anyapi' else 25,'Description':desc,'Url':'','Sha256':sha(path.read_bytes()),'FileHashes':{target:sha(data)},'GameBuilds':[build]}
 catalog['Api' if ident=='anyapi' else 'Mods'].append(entry)
(out/'catalog.json').write_text(json.dumps(catalog,indent=2))
print('Prepared 1 API-only package and 4 independent mod packages. No settings, game assets or user data included.')
