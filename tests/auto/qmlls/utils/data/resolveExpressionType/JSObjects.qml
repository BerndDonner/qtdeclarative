import QtQuick

Item {
    function f() {
        let object = {
            a: 1,
            b: 2,
            c: 3,
        };

        return object.a + object.b
    }
}