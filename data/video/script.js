document.getElementById("tokenForm").addEventListener("submit", async function (e) {
    e.preventDefault();
    const token = document.getElementById("token").value;

    // console.log(token);
    try {
        const response = await fetch("/submit", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: `token=${encodeURIComponent(token)}`,
        });
        const result = await response.text();
        if(response.status === 200) {
            window.location.href = '/stream/';
            return;
        }
        document.getElementById("token_response").textContent = result;
    } catch (error) {
        console.error('Error:', error);
        document.getElementById("token_response").textContent = 'An error occurred. Please try again.';
    }
});
