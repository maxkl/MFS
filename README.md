# MFS
Magic File System

## Building
Requires: `cmake`, `make`

```bash
mkdir build
cd build
cmake ..
make
```

## Running

```bash
./MFS COMMAND [OPTIONS] FILENAME
```

e.g.
```bash
./MFS create bs=1024 test.img
./MFS dump test.img
```
