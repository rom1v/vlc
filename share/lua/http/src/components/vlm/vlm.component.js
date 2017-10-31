import { bus } from '../../services/bus.service.js';
import { sendCommand } from '../../services/command.service.js';

Vue.component('vlm-modal', {
    template: '#vlm-modal-template',
    methods: {
        executeVLM() {
            const cmd = $('#vlmCommand').val();
            sendCommand(2, `?command=${cmd}`);
        }
    },
    created() {
        bus.$on('executeVLM', () => {
            this.executeVLM();
        });
    }
});
