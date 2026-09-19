"""Versioned identity metadata. This module never writes native game files."""
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import socket
import tempfile
import uuid


class FrontendError(ValueError):
    pass


def now():
    return datetime.now(timezone.utc).isoformat()


def new_id():
    return str(uuid.uuid4())


def empty_manifest():
    return {"schema_version": 1, "hunters": {}, "active_hunter": None,
            "instances": {}, "associations": {}, "host_settings": {}}


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise FrontendError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def valid_id(value):
    try:
        return isinstance(value, str) and str(uuid.UUID(value)) == value
    except ValueError:
        return False


def validate_locator(locator):
    if not isinstance(locator, dict) or locator.get('path_flavor') not in ('posix', 'nt'):
        raise FrontendError('invalid path flavor or locator')
    path = locator.get('path')
    if not isinstance(path, str) or not path or '\x00' in path:
        raise FrontendError('invalid installation locator')
    path_type = PurePosixPath if locator['path_flavor'] == 'posix' else PureWindowsPath
    parsed = path_type(path)
    if not parsed.is_absolute() or '..' in parsed.parts:
        raise FrontendError('locator must be absolute without parent traversal')
    return parsed


def validate_engine_evidence(evidence):
    if not isinstance(evidence, list):
        raise FrontendError('invalid engine evidence')
    paths = set()
    for item in evidence:
        if not isinstance(item, dict):
            raise FrontendError('invalid engine evidence entry')
        path, digest = item.get('path'), item.get('sha256')
        if (not isinstance(path, str) or not path or path in ('.', '..')
                or any(c in path for c in '/\\:\x00') or path in paths
                or not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest)
                or item.get('semantics') != 'unknown'):
            raise FrontendError('invalid engine evidence entry')
        paths.add(path)


def validate(data):
    if not isinstance(data, dict) or type(data.get("schema_version")) is not int or data["schema_version"] != 1:
        raise FrontendError("unsupported manifest schema; explicit migration required")
    for table in ("hunters", "instances", "associations", "host_settings"):
        if not isinstance(data.get(table), dict):
            raise FrontendError(f"invalid {table} object")
    for table in ("hunters", "instances", "associations"):
        for key, item in data[table].items():
            if not valid_id(key) or not isinstance(item, dict) or item.get("id") != key:
                raise FrontendError(f"invalid identity in {table}")
    for hunter in data["hunters"].values():
        if not isinstance(hunter.get("name"), str) or not hunter["name"].strip():
            raise FrontendError("hunter name is required")
    active = data.get("active_hunter")
    if active is not None and (not isinstance(active, str) or active not in data["hunters"] or data["hunters"][active].get("archived_at")):
        raise FrontendError("active hunter is missing or archived")
    paths = set()
    def revision_valid(value):
        return (isinstance(value, dict) and value.get('algorithm') == 'huntdat-sha256-v1'
                and isinstance(value.get('sha256'), str)
                and re.fullmatch(r'[0-9a-f]{64}', value['sha256'])
                and type(value.get('file_count')) is int and value['file_count'] >= 0
                and type(value.get('byte_count')) is int and value['byte_count'] >= 0)

    for instance in data["instances"].values():
        if instance.get("mode") not in ("managed", "registered"):
            raise FrontendError("invalid installation mode")
        installation_path = validate_locator(instance)
        path = instance['path']
        managed = instance.get('managed_root')
        if managed is not None:
            managed_path = validate_locator(managed)
            if instance['mode'] == 'managed' and (managed['path_flavor'] != instance['path_flavor']
                    or installation_path == managed_path or not installation_path.is_relative_to(managed_path)):
                raise FrontendError('managed installation must be below its recorded managed root')
        if instance.get('dialect_hint') not in ('unknown', 'c2-classic', 'iceage-triassic', 'mee-older', 'mee-newer'):
            raise FrontendError('invalid dialect hint')
        key = (instance.get("path_flavor"), path)
        if key in paths:
            raise FrontendError("duplicate installation locator")
        paths.add(key)
        if not isinstance(instance.get("revisions"), list) or not instance["revisions"]:
            raise FrontendError("installation revision history required")
        if instance.get("revision") not in instance["revisions"]:
            raise FrontendError("current revision absent from history")
        if not all(revision_valid(value) for value in instance['revisions']):
            raise FrontendError('invalid content revision')
        validate_engine_evidence(instance.get('engine_evidence'))
        reviews = instance.get('engine_relocation_reviews', [])
        if not isinstance(reviews, list):
            raise FrontendError('invalid engine relocation review history')
        for review in reviews:
            if (not isinstance(review, dict) or review.get('status') != 'required'
                    or not isinstance(review.get('observed_at'), str) or not review['observed_at']):
                raise FrontendError('invalid engine relocation review')
            validate_locator(review.get('from'))
            validate_locator(review.get('to'))
            validate_engine_evidence(review.get('baseline_engine_evidence'))
            validate_engine_evidence(review.get('destination_engine_evidence'))
            if review['baseline_engine_evidence'] != instance['engine_evidence']:
                raise FrontendError('engine relocation review must retain registration baseline')
    referenced = set()
    for association in data["associations"].values():
        if not isinstance(association.get('hunter_id'), str) or not isinstance(association.get('instance_id'), str):
            raise FrontendError('invalid association reference')
        if association.get("hunter_id") not in data["hunters"] or association.get("instance_id") not in data["instances"]:
            raise FrontendError("dangling state association")
        if association.get("ownership") not in ("referenced", "managed"):
            raise FrontendError("invalid state ownership")
        if not isinstance(association.get("state_key"), str):
            raise FrontendError("invalid native state key")
        if type(association.get('filename_slot')) is not int or association['filename_slot'] < 0:
            raise FrontendError('invalid filename slot')
        if association.get('origin') not in ('personal', 'bundled-example', 'unknown'):
            raise FrontendError('invalid origin declaration')
        if association.get('writable') is not False:
            raise FrontendError('schema 1 does not authorize native-state writers')
        authority = 'native-files' if association['ownership'] == 'referenced' else 'independent-snapshot'
        if association.get('authority') != authority or not revision_valid(association.get('revision')):
            raise FrontendError('invalid association authority or revision')
        if not isinstance(association.get("files"), list) or not association["files"]:
            raise FrontendError("state files required")
        for entry in association["files"]:
            path = entry.get("path") if isinstance(entry, dict) else None
            if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts or "\\" in path or ":" in path:
                raise FrontendError("unsafe state member path")
            digest = entry.get("sha256")
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise FrontendError("invalid state digest")
            if entry.get('kind') not in ('sav', 'sab') or type(entry.get('size')) is not int or entry['size'] < 0:
                raise FrontendError('invalid native state member')
        if len({entry['path'].casefold() for entry in association['files']}) != len(association['files']):
            raise FrontendError('ambiguous association member paths')
        if association["ownership"] == "referenced":
            key = (association["instance_id"], association["state_key"])
            if key in referenced:
                raise FrontendError("native state already associated; use an explicit independent copy")
            referenced.add(key)
    return data


def read_manifest(path):
    try:
        return validate(json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FrontendError(f"cannot read manifest: {error}") from error


def atomic_write(path, content):
    """Same-volume replacement; an interrupted write leaves the old file intact."""
    fd, temporary = tempfile.mkstemp(prefix=".pending-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        if os.name == "posix":
            directory = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


class Store:
    def __init__(self, directory):
        self.directory = Path(directory).expanduser().resolve()
        self.path = self.directory / "lodge.json"

    def read(self):
        if not self.path.exists() and self.path.with_suffix('.json.bak').exists():
            raise FrontendError('manifest missing with backup present; use explicit recovery')
        return read_manifest(self.path) if self.path.exists() else empty_manifest()

    @contextmanager
    def lock(self):
        self.directory.mkdir(parents=True, exist_ok=True)
        lock = self.directory / "lodge.lock"
        try:
            fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError as error:
            raise FrontendError(f"frontend writer lock exists: {lock}; verify its owner before manual recovery") from error
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                json.dump({"pid": os.getpid(), "host": socket.gethostname(), "created_at": now()}, stream)
                stream.flush()
                os.fsync(stream.fileno())
            yield
        finally:
            lock.unlink()

    @contextmanager
    def transaction(self):
        with self.lock():
            data = self.read()
            before = json.dumps(data, sort_keys=True)
            yield data
            validate(data)
            if not self.path.exists() or json.dumps(data, sort_keys=True) != before:
                if self.path.exists():
                    atomic_write(self.path.with_suffix(".json.bak"), self.path.read_bytes())
                atomic_write(self.path, (json.dumps(data, indent=2, ensure_ascii=True, allow_nan=False) + "\n").encode("utf-8"))

    def restore_backup(self):
        with self.lock():
            backup = self.path.with_suffix(".json.bak")
            read_manifest(backup)
            if self.path.exists():
                atomic_write(self.directory / f"lodge.recovery-{new_id()}.json", self.path.read_bytes())
            atomic_write(self.path, backup.read_bytes())


def hunter(data, action, identity=None, name=None):
    if action in ("create", "rename") and (not isinstance(name, str) or not name.strip()):
        raise FrontendError("a nonblank hunter display name is required")
    if action == "create":
        identity = new_id()
        data["hunters"][identity] = {"id": identity, "name": name, "created_at": now()}
    elif identity not in data["hunters"]:
        raise FrontendError("unknown hunter ID")
    if action == "rename":
        data["hunters"][identity]["name"] = name
    elif action == "archive":
        data["hunters"][identity]["archived_at"] = now()
        if data["active_hunter"] == identity:
            data["active_hunter"] = None
    elif action in ("select", "create"):
        if data["hunters"][identity].get("archived_at"):
            raise FrontendError("archived hunters cannot be selected")
        data["active_hunter"] = identity
    elif action != "rename":
        raise FrontendError("unknown hunter action")
    return data["hunters"][identity]
