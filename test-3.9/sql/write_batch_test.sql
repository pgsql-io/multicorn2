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

CREATE OR REPLACE FUNCTION notify_insert()
RETURNS TRIGGER AS $$
BEGIN
    RAISE NOTICE 'Inserted Record: %, %', NEW.fname1, NEW.fname2;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER after_insert_trigger
AFTER INSERT ON testmulticorn_write
FOR EACH ROW
EXECUTE FUNCTION notify_insert();

INSERT INTO testmulticorn_write(fname1, fname2)
VALUES
('col1_val6', 'col2_val'),
('col1_val7', 'col2_val');

DROP TRIGGER after_insert_trigger ON testmulticorn_write;
DROP FUNCTION notify_insert();

DROP USER MAPPING FOR current_user SERVER multicorn_srv;
DROP EXTENSION multicorn cascade;
