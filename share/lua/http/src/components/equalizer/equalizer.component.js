const data = {
    eqVal: 0
};

Vue.component('equalizer-modal', {
    template: '#equalizer-modal-template',
    data() {
        return data;
    },
    methods: {
        handleEvents() {
            const equalizerElement = $('#equalizerInput');
            data.eqVal = equalizerElement[0].value;
            equalizerElement.on('input', (e) => {
                data.eqVal = e.currentTarget.value;
            });
        }
    },
    mounted() {
        this.handleEvents();
    }
});
