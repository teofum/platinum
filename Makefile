relwithdebinfo:
	cmake -B./build/ -DCMAKE_BUILD_TYPE=RelWithDebInfo .
	cp ./build/compile_commands.json .
	make -C ./build/

release:
	cmake -B./build_release/ -DCMAKE_BUILD_TYPE=Release .
	cp ./build_release/compile_commands.json .
	make -C ./build_release/

debug:
	cmake -B./build_debug/ -DCMAKE_BUILD_TYPE=Debug .
	cp ./build_debug/compile_commands.json .
	make -C ./build_debug/

