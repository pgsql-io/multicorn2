# Installation

## Requirements

- Postgresql 14++
- Postgresql development packages
- Python development packages
- python >= 3.9 as your default python

### With the `pgxn client`:
```
   pgxn install multicorn
```
### From pgxn:
```
   wget multicorn_pgxn_download
   unzip multicorn-multicorn_release
   cd multicorn-multicorn_release
   make && sudo make install
```
### From source:
```
    git clone git://github.com/pgsql-io/multicorn2.git
    cd multicorn2
    make && make install
```
