# WIP

# partclone-nbd
Direct mount partclone image via nbdkit

## Usage

```
mkdir build; cd build
rm -rf *; cmake ..; make; nbdkit -fv -r --filter=./libnbdkit-partlcone-filter.so file /nfs/bt/ezio-nbd/2021-09-24-14-img-ubuntu-xz/vda2.ext4-ptcl-img
```

## License

partclone-nbd, mount partclone image via NBD directly
Copyright (C) 2021 Date Huang

This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along with this library; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
