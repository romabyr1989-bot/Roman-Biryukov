function send() {

    let data = {
        age: +age.value,
        updrs_iii: +updrs.value,
        moca: +moca.value,
        scopa_aut: +scopa.value,
        hoehn_yahr: +hy.value,

        ndufa4l2: +ndufa4l2.value,
        ndufs2: +ndufs2.value,
        pink1: +pink1.value,
        ppargc1a: +ppargc1a.value,
        nlrp3: +nlrp3.value,
        il1b: +il1b.value,
        s100a8: +s100a8.value,
        cxcl8: +cxcl8.value
    };

    fetch('/predict', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    })
    .then(r => r.json())
    .then(res => {
        isp.innerText = res.isp.toFixed(3);
        risk.innerText = (res.risk_probability * 100).toFixed(1) + "%";
        category.innerText = res.category;

        if (res.category === "HIGH") category.style.color = "red";
        else if (res.category === "INTERMEDIATE") category.style.color = "orange";
        else category.style.color = "green";
    });
}