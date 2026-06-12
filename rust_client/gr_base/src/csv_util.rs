use csv::ReaderBuilder;
use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::Read;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CsvRow {
    pub items: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CsvInfo {
    pub rows: Vec<CsvRow>,
}

pub fn read_csv_file(path: String) -> Result<CsvInfo, String> {
    let file = File::open(path);
    if let Err(err) = file {
        return Err(err.to_string());
    }
    let mut file = file.unwrap();
    let mut contents = String::new();
    if let Err(err) = file.read_to_string(&mut contents) {
        return Err(err.to_string());
    }
    println!("file contents:{:?}", contents);
    let mut reader = ReaderBuilder::new().from_reader(contents.as_bytes());

    println!("header: {:?}", reader.headers());

    for result in reader.records() {
        let record = result;
        if let Err(err) = record {
            return Err(err.to_string());
        }
        // 处理每一行的数据
        println!("{:?}", record);
    }

    Ok(CsvInfo { rows: vec![] })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::OpenOptions;
    use std::io::Write;

    #[test]
    fn test_read_csv_file() {
        //let mut r = OpenOptions::new().write(true).create(true).open("sample.csv").unwrap();
        //r.write("xxxx".as_bytes()).unwrap();
        if let Err(e) = read_csv_file(String::from("test/Book1.csv")) {
            println!("***error*** {}", e);
        }
    }
}
