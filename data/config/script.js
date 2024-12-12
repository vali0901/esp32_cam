document.getElementById("wifiForm").addEventListener("submit", async function (e) {
    e.preventDefault();
    const ssid = document.getElementById("ssid").value;
    const password = document.getElementById("password").value;
    const token = document.getElementById("token").value;

    const response = await fetch("/submit", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&token=${encodeURIComponent(token)}`,
    });

    const result = await response.text();
    document.getElementById("response").textContent = result;
});

document.getElementById('goToTokenMgmt').addEventListener('click', () => {
    fetch('/token_mgmt/')
        .then(response => {
            if (response.ok) {
                window.location.href = '/token_mgmt/';
            } else {
                alert('Failed to navigate to Token Management. Please try again.');
            }
        })
        .catch(error => {
            console.error('Error:', error);
            alert('An error occurred. Please try again.');
        });
});

document.getElementById('quit').addEventListener('click', async () => {
    const response = await fetch('/quit', {
        method: 'POST',
    });

    const result = await response.text();
    document.getElementById('response').textContent = result;
});

