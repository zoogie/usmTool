# usmTool

- Tool that is downloaded by certain versions of browserhax as a boot.3dsx (ex. https://zoogie.github.io/web/full) 
- Once running, it downloads most or all of the files from [Finalizing Setup](https://3ds.hacks.guide/finalizing-setup.html) and installs usm to wifi slots. 
- This uses 3ds-curl to download binaries directly from github - the same code used by FBI and others.
- The goal is to have the fastest and easiest cfw install possible, rivaling methods used on Wii and PSVita. My tests have shown about a 3 minute install from stock to cfw. A more basic version could be lot faster if most of the non-essential homebrew apps are skipped.
- [Here](https://github.com/zoogie/usmTool/blob/main/source/main.c#L318) is the list of apps that are downloaded currently.