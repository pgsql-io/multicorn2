CREATE EXTENSION multicorn;
CREATE server multicorn_srv foreign data wrapper multicorn options (
    wrapper 'multicorn.testfdw.TestForeignDataWrapper'
);
CREATE user mapping FOR current_user server multicorn_srv options (usermapping 'test');

CREATE foreign table testmulticorn_write(
    fname1 character varying,
    fname2 character varying
) server multicorn_srv options (
    option1 'option1',
    row_id_column 'test1',
    modify_batch_size '2'
);

insert into testmulticorn_write(fname1, fname2)
VALUES
('col1_val1', 'col2_val'),
('col1_val2', 'col2_val'),
('col1_val3', 'col2_val'),
('col1_val4', 'col2_val'),
('col1_val5', 'col2_val');

DROP foreign table testmulticorn_write;

CREATE foreign table testmulticorn_write(
    fname1 character varying,
    fname2 character varying
) server multicorn_srv options (
    option1 'option1',
    row_id_column 'test1',
    test_type 'returning',
    modify_batch_size '2'
);

insert into testmulticorn_write (fname1, fname2)
VALUES
('col1_val1', 'col2_val'),
('col1_val2', 'col2_val'),
('col1_val3', 'col2_val'),
('col1_val4', 'col2_val'),
('col1_val5', 'col2_val')
RETURNING fname1, fname2;

DROP USER MAPPING FOR current_user SERVER multicorn_srv;
DROP EXTENSION multicorn cascade;
