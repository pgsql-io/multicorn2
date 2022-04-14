Multicorn2
==========

Synopsis
--------

Multicorn2 is a PostgreSQL 10+ extension allowing to write Foreign Data Wrappers
in python.
It is bundled with some foreign data wrappers.


Usage
-----

Create the extension (as a super user, on your target database):


    CREATE EXTENSION multicorn2;



Define a foreign server for the specific python foreign data wrappers you want
to use:



    CREATE SERVER my_server_name FOREIGN DATA WRAPPER multicorn2
    options (
        wrapper 'python.class.Name'
    )


Where *python.class.Name* is a string defining which foreign data wrapper class
to use.

Ex, for the Imap foreign data wrapper:



    CREATE SERVER multicorn2_imap FOREIGN DATA WRAPPER multicorn2
        options (
        wrapper 'multicorn2.imapfdw.ImapFdw'
    );


Once you have a server set up, you can create foreign tables on your server.

The foreign table must be supplied its required options.

Ex:



    create foreign table gmail (                                                                 
        "Message-ID" character varying,
        "From" character varying,
        "Subject" character varying,
        "payload" character varying,
        "flags" character varying[],
        "To" character varying) server multicorn2_imap options (
            host 'imap.gmail.com',
            port '465', 
            payload_column 'payload', 
            flags_column 'flags',
            ssl 'True',
            login 'mylogin', 
            password 'mypassword'
    );
