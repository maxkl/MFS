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
./MFS FILENAME COMMAND [OPTIONS]
```

e.g.
```bash
$ ./MFS test.img create bs=128 bc=128
$ ./MFS test.img repl
> info
> ls /
> mkdir /test
> touch /test/hi.txt
> ls /test
> sync
> exit
```
