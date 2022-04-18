************
Installation
************

Requirements
============

- Postgresql 10++
- Postgresql development packages
- Python development packages
- python >= 3.6 as your default python

With the `pgxn client`_::

   pgxn install multicorn

From pgxn:

.. parsed-literal::

   wget |multicorn_pgxn_download|
   unzip multicorn-|multicorn_release|
   cd multicorn-|multicorn_release|
   make && sudo make install

From source::

    git clone git://github.com/pgsql-io/Multicorn2.git
    cd Multicorn2
    make && make install

.. _pgxn client: http://pgxnclient.projects.postgresql.org/

