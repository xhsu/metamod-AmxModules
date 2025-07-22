import py7zr;
import sys;

RESOURCE_PACKER_VER = 20250722;

reslist: list[str] = [];
gamedir: str = "";

if len(sys.argv) == 2:
	with open(sys.argv[1], 'r', encoding='utf-8') as f:
		while line := f.readline():
			reslist.append(line.strip());

	gamedir = reslist[0];
	reslist = reslist[1:];

	with py7zr.SevenZipFile("Archive.7z", 'w') as archive:
		for res in reslist:
			pos = res.rfind("..\\");
			assert(pos != -1);

			if res.count("..\\") <= 3:	# in original gamedir
				relpath = res[pos + 3:].strip();
			else:	# under other path
				pos2 = res.find('\\', pos + 3);
				assert(pos2 != -1);

				relpath = res[pos2 + 1:].strip();

			print(relpath);
			try:
				archive.write(res, relpath);
			except Exception as e:
				print(">>> File no found!");
		# END_FOR
	# END_WITH
# END_IF
else:
	print("Drop res list on me!");
