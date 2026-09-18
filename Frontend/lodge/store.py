"""Versioned identity metadata. This module never writes native game files."""
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
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
    if active is not None and (active not in data["hunters"] or data["hunters"][active].get("archived_at")):
        raise FrontendError("active hunter is missing or archived")
    paths = set()
    for instance in data["instances"].values():
        if instance.get("mode") not in ("managed", "registered"):
            raise FrontendError("invalid installation mode")
        path = instance.get("path")
        if not isinstance(path, str) or not path:
            raise FrontendError("invalid installation locator")
        key = (instance.get("path_flavor"), path)
        if key in paths:
            raise FrontendError("duplicate installation locator")
        paths.add(key)
        if not isinstance(instance.get("revisions"), list) or not instance["revisions"]:
            raise FrontendError("installation revision history required")
        if instance.get("revision") not in instance["revisions"]:
            raise FrontendError("current revision absent from history")
    referenced = set()
    for association in data["associations"].values():
        if association.get("hunter_id") not in data["hunters"] or association.get("instance_id") not in data["instances"]:
            raise FrontendError("dangling state association")
        if association.get("ownership") not in ("referenced", "managed"):
            raise FrontendError("invalid state ownership")
        if not isinstance(association.get("state_key"), str):
            raise FrontendError("invalid native state key")
        if not isinstance(association.get("files"), list) or not association["files"]:
            raise FrontendError("state files required")
        for entry in association["files"]:
            path = entry.get("path") if isinstance(entry, dict) else None
            if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts or "\\" in path or ":" in path:
                raise FrontendError("unsafe state member path")
            digest = entry.get("sha256")
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise FrontendError("invalid state digest")
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
