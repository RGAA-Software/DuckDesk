use mail_send::mail_builder::MessageBuilder;
use mail_send::SmtpClientBuilder;

pub async fn send_email() {
    // Build a simple multipart message
    let message = MessageBuilder::new()
        .from(("John Doe", "john@example.com"))
        .to(vec![
            ("Jane Doe", "jane@example.com"),
            ("James Smith", "james@test.com"),
            ("James Smith", "huayang@dolit.cn"),
        ])
        .subject("Hi!")
        .html_body("<h1>Hello, world!</h1>")
        .text_body("Hello world!");

    // Connect to the SMTP submissions port, upgrade to TLS and
    // authenticate using the provided credentials.
    SmtpClientBuilder::new("smtp.gmail.com", 587)
        .implicit_tls(false)
        .credentials(("john", "p4ssw0rd"))
        .connect()
        .await
        .unwrap()
        .send(message)
        .await
        .unwrap();
}

#[cfg(test)]
#[allow(non_snake_case)]
mod tests {
    use super::*;

    macro_rules! aw {
        ($e:expr) => {
            tokio_test::block_on($e)
        };
    }

    #[test]
    fn it_works() {
        aw!(send_email());
    }
}
