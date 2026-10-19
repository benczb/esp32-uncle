"""Official Muse v5 transcripts and isolated storage, using adapted upstream tests."""
import pathlib
import unittest

if __name__ == '__main__':
    suite = unittest.defaultTestLoader.discover(str(pathlib.Path(__file__).parent / 'muse_pairing'), pattern='test_*.py')
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    raise SystemExit(not result.wasSuccessful())
