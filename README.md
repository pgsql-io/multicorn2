
Multicorn2
==========

Multicorn Python3 Foreign Data Wrapper (FDW) for Postgresql.  Tested on Linux w/ Python 3.9-3.12 & Postgres 13-17.

Testing is underway for supporting Python 3.13 and is expected in v3.1.  Newest versions of major linux distro's (Debian 12, Ubuntu 24.04 & EL10) are all still using Python 3.12 so sticking with using 3.12 is advised in the short run.

The Multicorn Foreign Data Wrapper allows you to fetch foreign data in Python in your PostgreSQL server.

Multicorn2 is distributed under the PostgreSQL license. See the LICENSE file for
details.

## How it works

In use, Multicorn includes a Python package which contains:

- A `__init__.py` which provides a base class from which your new,
  custom, Extension will derive.
- A `utils.py` containing diagnostic support code your Extension can use.
- Several useful example FDW implementations.

Multicorn also includes, under the covers, **two** shared libraries:

- `multicorn.so` contains a generic Postgres FDW extension which
  interfaces between Postgres and your custom FDW.
- `_utils.so` is a CPython extension which provides support for
  the previously mentioned `utils.py`.

## Using in PGEDGE (see https://github.com/pgedge/pgedge)

1.) Install pgEdge from the command line, in your home directory, with the curl command at the top of pgedge/pgedge

2.) Change into the pgedge directory and install pgXX
```bash
cd pgedge
./pgedge install pg17 --start
./pgedge install multicorn2
```

3.) Use multicorn as you normally would AND you can install popular FDW's that use multicorn such as ElasticSerachFDW & BigQueryFDW
```bash
      ./pgedge install mqttclient
```

## Building Multicorn2 against Postgres from Source

It is built the same way all standard postgres extensions are built with following dependcies needing to be installed:

### Install Dependencies for Building Postgres & the Multicorn2 extension
On Debian/Ubuntu systems:
```bash
sudo apt install -y build-essential libreadline-dev zlib1g-dev flex bison libxml2-dev libxslt-dev libssl-dev libxml2-utils xsltproc
sudo apt install -y python3 python3-dev python3-setuptools python3-pip
```

On CentOS/Rocky/Redhat systems:
```bash
sudo yum install -y bison-devel readline-devel libedit-devel zlib-devel openssl-devel bzip2-devel libmxl2 libxslt-devel wget
sudo yum groupinstall -y 'Development Tools'

sudo yum -y install git python3 python3-devel python3-pip
```

### Upgrade to latest PIP (recommended)
```bash
cd ~
python3 -m venv venv
source venv/bin/activate
```

### Download & Compile Postgres 13+ source code
```bash
cd ~
wget https://ftp.postgresql.org/pub/source/v17.0/postgresql-17.0.tar.gz
tar -xvf postgresql-17.0.tar.gz
cd postgresql-17.0
./configure
make
sudo make install
```

### Download & Compile Multicorn2
```bash
set PATH=/usr/local/pgsql/bin:$PATH
cd ~/postgresql-17.0/contrib
wget https://github.com/pgsql-io/multicorn2/archive/refs/tags/v3.0.tar.gz
tar -xvf v3.0.tar.gz
cd multicorn2-3.0
make
sudo make install
```

### Create Multicorn2 Extension
In your running instance of Postgres from the PSQL command line
```sql
CREATE EXTENSION multicorn;
```

## Building Multicorn2 against pre-built Postgres

When using a pre-built Postgres installed using your OS package manager, you will need the additional package that enables Postgres extensions to be built:

### Install Dependencies for Building the Multicorn2 extension
On Debian/Ubuntu systems:
```bash
sudo apt install -y build-essential ... postgresql-server-dev-13
sudo apt install -y python3 python3-dev python3-setuptools python3-pip
```

On CentOS/Rocky/Redhat systems:
```bash
sudo yum install -y ... <postgres server dev package>
sudo yum groupinstall -y 'Development Tools'
sudo yum -y install git python3 python3-devel python3-pip
```

### Download & Compile Multicorn2
```bash
wget https://github.com/pgsql-io/multicorn2/archive/refs/tags/v3.0.tar.gz
tar -xvf v3.0.tar.gz
cd multicorn2-3.0
make
sudo make install
```

Note that the last step installs both the extension into Postgres and also the Python part. The latter is done using "pip install", and so can be undone using "pip uninstall".

### Create Multicorn2 Extension
In your running instance of Postgres from the PSQL command line
```sql
CREATE EXTENSION multicorn;
```

## Integration tests

multicorn2 has an extensive suite of integration tests which run against live PostgreSQL servers.  In order to manage the matrix of supported versions of Python and PostgreSQL, the Nix package manager can be used to provide all the dependencies and run all the tests.  The Nix package manager is supported on Linux, MacOS, and Windows Subsystem for Linux.  To install Nix, follow the instructions at https://nixos.org/download/.

Once Nix is installed, you can run the tests with the following commands.  To run a complete regression test against all supported versions of Python and PostgreSQL, run:

```bash
nix build .#allTestSuites
```

This can be slow to run when it is first executed (15-30 minutes) due to the need to rebuild PostgreSQL with specific versions of Python, to support the plpython extension which is used in some tests.  Subsequent runs will be faster (under 5 minutes) as the build artifacts are cached.

To run a faster test suite against a specific version of Python and PostgreSQL, run:

```bash
nix build .#testSuites.test_pg13_py39
```

**Known issues:**
- The tests cover the supported range of Python & PostgreSQL combinations;

### Adding new Python or PostgreSQL versions to the test suite

1. Perform a `nix flake update` in order to provide access to the latest packages available from the Nix package manager.

2. Update the supported list of versions in flake.nix under the variable `testPythonVersions` or `testPostgresVersions`.

3. For new Python versions, create a new symlink to test-3.9 with the version number of the new Python version; and update the list of test directories in `makeTestSuite` in flake.nix.

4. Run the tests with `nix build .#allTestSuites` to ensure that the new versions are supported.
