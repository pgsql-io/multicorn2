from logging import ERROR, INFO, DEBUG, WARNING, CRITICAL
try:
    from ._utils import _log_to_postgres
    from ._utils import check_interrupts
except ImportError as e:
    from warnings import warn
    warn("Not executed in a postgresql server,"
         " disabling log_to_postgres", ImportWarning)

    def _log_to_postgres(message, level=0, hint=None, detail=None):
        pass


REPORT_CODES = {
    DEBUG: 0,
    INFO: 1,
    WARNING: 2,
    ERROR: 3,
    CRITICAL: 4
}


class MulticornException(Exception):
    def __init__(self, message, code, hint, detail):
        self._is_multicorn_exception = True
        self.message = message
        self.code = code
        self.hint = hint
        self.detail = detail


def log_to_postgres(message, level=INFO, hint=None, detail=None):
    code = REPORT_CODES.get(level, None)
    if code is None:
        raise KeyError("Not a valid log level")
    if level in (ERROR, CRITICAL):
        # if we sent an ERROR or FATAL(=CRITICAL) message to _log_to_postgres, we would trigger the PostgreSQL C-level
        # exception handling, which would prevent us from cleanly exiting whatever Python context we're currently in.
        # To avoid this, these log levels are replaced with exceptions which are bubbled back to Multicorn's entry
        # points, and those exceptions are translated into appropriate logging after we exit the method at the top of
        # the multicorn stack.
        raise MulticornException(message, code, hint, detail)
    else:
        _log_to_postgres(message, code, hint=hint, detail=detail)
