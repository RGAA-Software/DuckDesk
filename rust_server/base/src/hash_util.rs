use digest::Digest;

pub enum HashAlgo {
    MD5,
    SHA1,
    SHA256,
    SHA512,
}

pub fn compute_hash(algo: HashAlgo, data: &[u8]) -> String {
    match algo {
        HashAlgo::MD5 => {
            let mut hasher = md5::Md5::new();
            hasher.update(data);
            format!("{:x}", hasher.finalize())
        }
        HashAlgo::SHA1 => {
            let mut hasher = sha1::Sha1::new();
            hasher.update(data);
            format!("{:x}", hasher.finalize())
        }
        HashAlgo::SHA256 => {
            let mut hasher = sha2::Sha256::new();
            hasher.update(data);
            format!("{:x}", hasher.finalize())
        }
        HashAlgo::SHA512 => {
            let mut hasher = sha2::Sha512::new();
            hasher.update(data);
            format!("{:x}", hasher.finalize())
        }
    }
}