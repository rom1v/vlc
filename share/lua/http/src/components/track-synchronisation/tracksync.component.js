const trackData = {
    playbackVal: 0,
    audioDelayVal: 0,
    subDelayVal: 0
};

let playbackElement;
let audioDelayElement;
let subDelayElement;

Vue.component('track-sync-modal', {
    template: '#track-sync-modal-template',
    data() {
        return trackData;
    },
    methods: {
        handleEvents: () => {
            trackData.playbackVal = playbackElement[0].value;

            trackData.audioDelayVal = audioDelayElement[0].value;

            trackData.subDelayVal = subDelayElement[0].value;

            playbackElement.on('input', (e) => {
                trackData.playbackVal = e.currentTarget.value;
            });

            audioDelayElement.on('input', (e) => {
                trackData.audioDelayVal = e.currentTarget.value;
            });

            subDelayElement.on('input', (e) => {
                trackData.subDelayVal = e.currentTarget.value;
            });
        }
    },
    mounted() {
        playbackElement = $('#playbackInput');
        audioDelayElement = $('#audioDelayInput');
        subDelayElement = $('#subDelayInput');
        this.handleEvents();
    }
});
