# Server Installation & Configuration

We recommend that users with adequate money choose large foreign cloud service providers such as Amazon and Azure, which generally do not have their IPs blocked. Do not use cloud computing services from unknown sources. Proxy servers take up very little CPU and memory resources, and the final network speed depends mainly on the server's network bandwidth and line quality.

The proxy server software mita needs to run on Linux. We provide both debian and RPM installers for installing mita on Debian / Ubuntu and Fedora / CentOS / Red Hat Enterprise Linux series distributions.

Before installation and configuration, connect to the server via SSH and then execute the following commands.

## Download mita installation package

```sh
# Debian / Ubuntu
curl -LSO https://github.com/enfein/mieru/releases/download/v1.8.0/mita_1.8.0_amd64.deb

# Fedora / CentOS / Red Hat Enterprise Linux
curl -LSO https://github.com/enfein/mieru/releases/download/v1.8.0/mita-1.8.0-1.x86_64.rpm
```

To download the installer for the ARM architecture, replace `amd64` with `arm64`, and `x86_64` with `aarch64` in the link. If the above link is blocked, please use your browser to download and install from the GitHub Releases page.

## Install mita package

```sh
# Debian / Ubuntu
sudo dpkg -i mita_1.8.0_amd64.deb

# Fedora / CentOS / Red Hat Enterprise Linux
sudo rpm -Uvh --force mita-1.8.0-1.x86_64.rpm
```

## Grant permissions

```sh
sudo usermod -a -G mita $USER

sudo reboot
```

## After reboot, reconnect the server via SSH, check mita daemon status

```sh
systemctl status mita
```

If the output contains `active (running)`, it means that the mita daemon is already running. Normally, mita will start automatically after the server is booted.

## Check mita working status

```sh
mita status
```

If the installation is just completed, the output will be `mieru server status is "IDLE"`, indicating that mita has not yet listening to requests from the mieru client.

## Modify proxy server settings

The mieru proxy supports two different transport protocols, TCP and UDP. To understand the differences between the protocols, please read [mieru proxy protocols](https://github.com/enfein/mieru/blob/main/docs/protocol.md). This tutorial explains the TCP protocol as an example. To use the UDP protocol, replace `TCP` with `UDP` in all configuration files.

Users should call

```sh
mita apply config <FILE>
```

to modify the proxy server settings. Here `<FILE>` is a JSON formatted file. We provide a configuration template in the `configs/templates/server_config.json` file in the root of the project. The contents of this template are as follows.

```js
{
    "portBindings": [
        {
            "port": -1,
            "protocol": "TCP"
        }
    ],
    "users": [
        {
            "name": "<username@example.com>",
            "password": "<your-password>"
        }
    ],
    "loggingLevel": "INFO",
    "mtu": 1400
}
```

Please download or copy this template to your server, open it in a text editor, and modify the following fields.

1. The `portBindings` -> `port` property is the TCP or UDP port number that mita listens on, specify a value from 1025 to 65535. **Please make sure that the firewall allows communication using this port.**
2. fill in the `users` -> `name` property with the user name.
3. Fill in the `users` -> `password` property with the user's password.
4. The `mtu` property is the maximum data link layer payload size when using the UDP proxy protocol. The default value is 1400. You can choose a value between 1280 and 1500.

In addition to this, mita can listen to several different ports. You can also create multiple users if you want to share the proxy for others to use. Here is an example of the server configuration file, for reference only.

```js
{
    "portBindings": [
        {
            "port": 2012,
            "protocol": "TCP"
        },
        {
            "port": 2027,
            "protocol": "TCP"
        }
    ],
    "users": [
        {
            "name": "baozi",
            "password": "shilishanlubuhuanjian"
        },
        {
            "name": "ducaiguozei",
            "password": "xijinping"
        }
    ],
    "loggingLevel": "INFO",
    "mtu": 1400
}
```

Assuming that on the server, the configuration file name is `server_config.json`, call the command `mita apply config server_config.json` to write the configuration after the file is modified.

If there is an error in the configuration, mita will print the problem that occurred. Follow the prompts to modify the configuration file and re-run the `mita apply config <FILE>` command to write the configuration.

After that, invoke command

```sh
mita describe config
```

to check the current proxy settings.

## Start proxy service

Use command

```sh
mita start
```

to start proxy service. At this point, mita will start listening to the port number specified in the settings.

Then, use command

```sh
mita status
```

to check working status. If `mieru server status is "RUNNING"` is returned here, it means that the proxy service is running and can process client requests.

If you want to stop the proxy service, use command

```sh
mita stop
```

Note that each time you change the settings with `mita apply config <FILE>`, you need to restart the service with `mita stop` and `mita start` for the new settings to take effect.

After starting the proxy service, proceed to [Client Installation & Configuration](https://github.com/enfein/mieru/blob/main/docs/client-install.md).

## [Optional] Install NTP network time synchronization service

The client and proxy server software calculate the key based on the user name, password and system time. The server can decrypt and respond to the client's request only if the client and server have the same key. This requires that the system time of the client and the server must be in sync.

To ensure that the server system time is accurate, we recommend that users install the NTP network time service. In many Linux distributions, installing NTP is only one command.

```sh
# Debian / Ubuntu
sudo apt-get install ntp

# Fedora
sudo dnf install ntp

# CentOS / Red Hat Enterprise Linux
sudo yum install ntp
```
