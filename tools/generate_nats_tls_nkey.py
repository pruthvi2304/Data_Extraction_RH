from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
import argparse
import base64
import ipaddress
import json

import nkeys
from nacl.signing import SigningKey
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID


@dataclass(frozen=True)
class CertPaths:
    ca_key: Path
    ca_crt: Path
    server_key: Path
    server_crt: Path
    client_key: Path
    client_crt: Path


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def write_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def pem_private_key(key: rsa.RSAPrivateKey) -> bytes:
    return key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption(),
    )


def pem_cert(cert: x509.Certificate) -> bytes:
    return cert.public_bytes(serialization.Encoding.PEM)


def build_ca() -> tuple[rsa.RSAPrivateKey, x509.Certificate]:
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    now = datetime.now(timezone.utc)
    subject = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "IN"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Data Extraction DE"),
        x509.NameAttribute(NameOID.COMMON_NAME, "NATS Local CA"),
    ])
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=5))
        .not_valid_after(now + timedelta(days=3650))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False)
        .sign(private_key=key, algorithm=hashes.SHA256())
    )
    return key, cert


def build_leaf(cn: str,
               ca_key: rsa.RSAPrivateKey,
               ca_cert: x509.Certificate,
               is_client: bool,
               server_names: list[str] | None = None,
               server_ips: list[str] | None = None) -> tuple[rsa.RSAPrivateKey, x509.Certificate]:
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    now = datetime.now(timezone.utc)
    subject = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "IN"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Data Extraction DE"),
        x509.NameAttribute(NameOID.COMMON_NAME, cn),
    ])

    usages = [ExtendedKeyUsageOID.CLIENT_AUTH] if is_client else [ExtendedKeyUsageOID.SERVER_AUTH]
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(ca_cert.subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=5))
        .not_valid_after(now + timedelta(days=825))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
        .add_extension(x509.ExtendedKeyUsage(usages), critical=False)
    )

    if not is_client:
        names = ["localhost", "plant-nats", "host.docker.internal"]
        names.extend(server_names or [])
        ips = ["127.0.0.1"]
        ips.extend(server_ips or [])
        builder = builder.add_extension(
            x509.SubjectAlternativeName([
                *(x509.DNSName(name) for name in dict.fromkeys(names)),
                *(x509.IPAddress(ipaddress.ip_address(address)) for address in dict.fromkeys(ips)),
            ]),
            critical=False,
        )

    cert = builder.sign(private_key=ca_key, algorithm=hashes.SHA256())
    return key, cert


def generate_user_nkey() -> tuple[str, str]:
    signing_key = SigningKey.generate()
    seed = nkeys.encode_seed(signing_key.encode(), nkeys.PREFIX_BYTE_USER).decode("ascii")
    kp = nkeys.from_seed(seed.encode("ascii"))
    public_key = kp.public_key.decode("ascii")
    return seed, public_key


def block_from_file(path: Path, begin: str, end: str) -> str:
    """Return a PEM-style NATS credential block, or raw file content."""
    content = path.read_text(encoding="utf-8").strip()
    if begin in content:
        if end not in content:
            raise ValueError(f"{path} contains an incomplete credential block.")
        content = content.split(begin, 1)[1].split(end, 1)[0].strip()
    return "".join(content.split())


def user_jwt_from_file(path: Path) -> str:
    jwt = block_from_file(path, "-----BEGIN NATS USER JWT-----", "------END NATS USER JWT------")
    if jwt.count(".") != 2:
        raise ValueError(f"{path} does not contain a valid NATS user JWT.")
    return jwt


def user_seed_from_file(path: Path) -> str:
    seed = block_from_file(path, "-----BEGIN USER NKEY SEED-----", "------END USER NKEY SEED------")
    try:
        nkeys.from_seed(seed.encode("ascii"))
    except Exception as error:
        raise ValueError(f"{path} does not contain a valid user NKey seed.") from error
    return seed


def standard_creds(user_jwt: str, seed: str) -> str:
    return (
        "-----BEGIN NATS USER JWT-----\n"
        f"{user_jwt}\n"
        "------END NATS USER JWT------\n\n"
        "-----BEGIN USER NKEY SEED-----\n"
        f"{seed}\n"
        "------END USER NKEY SEED------\n"
    )


def verify_jwt_matches_seed(user_jwt: str, seed: str) -> None:
    """Reject a JWT and seed that identify different NATS users."""
    payload = user_jwt.split(".")[1]
    payload += "=" * (-len(payload) % 4)
    try:
        subject = json.loads(base64.urlsafe_b64decode(payload)).get("sub")
    except (ValueError, json.JSONDecodeError) as error:
        raise ValueError("Unable to read the NATS user JWT payload.") from error

    public_key = nkeys.from_seed(seed.encode("ascii")).public_key.decode("ascii")
    if subject != public_key:
        raise ValueError("The NATS user JWT subject does not match the supplied user NKey seed.")


def write_nkey_files(base_dir: Path, seed: str, public_key: str, credentials: str | None) -> None:
    write_text(base_dir / "user.seed", f"{seed}\n")
    write_text(base_dir / "user.public", f"{public_key}\n")

    # A valid .creds bundle must contain an operator/account-issued user JWT
    # and the private seed. A seed/public-key pair by itself is NKey auth, not
    # a usable NATS JWT credential.
    if credentials is not None:
        write_text(base_dir / "user.creds", credentials)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate NATS TLS and NKey development credentials.")
    parser.add_argument("--server-name", action="append", default=[],
                        help="Additional DNS SAN for the NATS server certificate. Repeat as needed.")
    parser.add_argument("--server-ip", action="append", default=[],
                        help="Additional IP SAN for the NATS server certificate. Repeat as needed.")
    credential_source = parser.add_mutually_exclusive_group()
    credential_source.add_argument("--user-creds-file", type=Path,
                                   help="Complete operator-issued .creds file to copy to certs/nats/user.creds.")
    credential_source.add_argument("--user-jwt-file", type=Path,
                                   help="Operator-issued NATS user JWT. Requires --user-seed-file for the matching seed.")
    parser.add_argument("--user-seed-file", type=Path,
                        help="NKey seed matching --user-jwt-file. Do not use with --user-creds-file.")
    args = parser.parse_args()
    repo_root = Path(__file__).resolve().parents[1]

    nats_cert_dir = repo_root / "certs" / "nats"
    publisher_cert_dir = repo_root / "certs" / "publisher"

    paths = CertPaths(
        ca_key=nats_cert_dir / "ca.key",
        ca_crt=nats_cert_dir / "ca.crt",
        server_key=nats_cert_dir / "server.key",
        server_crt=nats_cert_dir / "server.crt",
        client_key=publisher_cert_dir / "client.key",
        client_crt=publisher_cert_dir / "client.crt",
    )

    ca_key, ca_cert = build_ca()
    server_key, server_cert = build_leaf("plant-nats.local", ca_key, ca_cert, is_client=False,
                                         server_names=args.server_name, server_ips=args.server_ip)
    client_key, client_cert = build_leaf("publisher.local", ca_key, ca_cert, is_client=True)

    write_bytes(paths.ca_key, pem_private_key(ca_key))
    write_bytes(paths.ca_crt, pem_cert(ca_cert))
    write_bytes(paths.server_key, pem_private_key(server_key))
    write_bytes(paths.server_crt, pem_cert(server_cert))
    write_bytes(paths.client_key, pem_private_key(client_key))
    write_bytes(paths.client_crt, pem_cert(client_cert))
    write_bytes(publisher_cert_dir / "ca.crt", pem_cert(ca_cert))

    try:
        if args.user_creds_file:
            if args.user_seed_file:
                parser.error("--user-seed-file cannot be combined with --user-creds-file.")
            user_jwt = user_jwt_from_file(args.user_creds_file)
            seed = user_seed_from_file(args.user_creds_file)
            verify_jwt_matches_seed(user_jwt, seed)
            credentials = standard_creds(user_jwt, seed)
        elif args.user_jwt_file:
            if not args.user_seed_file:
                parser.error("--user-jwt-file requires --user-seed-file for the matching NKey.")
            user_jwt = user_jwt_from_file(args.user_jwt_file)
            seed = user_seed_from_file(args.user_seed_file)
            verify_jwt_matches_seed(user_jwt, seed)
            credentials = standard_creds(user_jwt, seed)
        else:
            seed, _ = generate_user_nkey()
            credentials = None
    except (OSError, ValueError) as error:
        parser.error(str(error))
    public_key = nkeys.from_seed(seed.encode("ascii")).public_key.decode("ascii")
    write_nkey_files(nats_cert_dir, seed, public_key, credentials)

    readme = (
        "Generated by tools/generate_nats_tls_nkey.py\n"
        "Keep ca.key, server.key, client.key, and user.seed private.\n"
        "ca.crt is the CA certificate for the generated local NATS server.\n"
    )
    readme += ("user.creds was generated from supplied operator-issued credentials.\n" if credentials else
               "No user.creds was generated: an operator-issued user JWT is required.\n")
    write_text(nats_cert_dir / "README.txt", readme)
    print("Generated TLS cert/key pair and NKey credentials.")
    print(f"NKey public key: {public_key}")
    print(f"CA certificate: {paths.ca_crt}")
    if credentials:
        print(f"NATS credentials file: {nats_cert_dir / 'user.creds'}")
    else:
        print("No .creds file generated. Supply --user-jwt-file with an operator-issued user JWT.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
